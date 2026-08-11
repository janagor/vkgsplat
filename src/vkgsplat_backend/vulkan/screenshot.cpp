#include "screenshot.hpp"

#include "app_state.hpp"
#include "vulkan/gpu_allocator.hpp"
#include "vulkan/initializers.hpp"
#include <vkgsplat_io/write_png.hpp>
#include "vulkan_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

namespace {

  void DestroyStaging(Context &context, Buffer &staging) { context.gpu_allocator.destroy_buffer(staging); }

  void FreeCmd(Context &context, RenderData &data, VkCommandBuffer cmd)
  {
    if (cmd != VK_NULL_HANDLE && data.command_pool.has_value()) {
      context.disp.freeCommandBuffers(data.command_pool->handle(), 1, &cmd);
    }
  }

}// namespace

auto SaveColorTargetPng(Context &context, RenderData &data, std::string_view path) -> std::expected<void, Error>
{
  if (data.color_image == VK_NULL_HANDLE || data.color_width == 0 || data.color_height == 0) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "no color target available for screenshot") };
  }
  if (data.color_format != VK_FORMAT_R8G8B8A8_UNORM) {
    return std::unexpected{ MakeError(std::errc::not_supported, "screenshot currently requires R8G8B8A8_UNORM") };
  }
  if (!data.command_pool.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "command pool missing for screenshot") };
  }

  if (data.frame_timeline.has_value()) {
    if (auto waited = data.frame_timeline->wait_value(data.next_timeline_value); !waited) {
      return std::unexpected{ waited.error() };
    }
  } else {
    context.disp.deviceWaitIdle();
  }

  constexpr u32 kChannels = 4;
  auto const byte_size = static_cast<VkDeviceSize>(data.color_width) * data.color_height * kChannels;
  auto staging = context.gpu_allocator.create_staging_buffer(byte_size);
  if (!staging) { return std::unexpected{ staging.error() }; }

  auto buffers = data.command_pool->allocate_buffers(1);
  if (!buffers) {
    DestroyStaging(context, *staging);
    return std::unexpected{ buffers.error() };
  }
  VkCommandBuffer cmd = buffers->front().handle();

  auto fail = [&](Error err) -> std::expected<void, Error> {
    FreeCmd(context, data, cmd);
    DestroyStaging(context, *staging);
    return std::unexpected{ std::move(err) };
  };

  VkCommandBufferBeginInfo const begin_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = nullptr,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = nullptr,
  };
  if (context.disp.beginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
    return fail(MakeError(std::errc::io_error, "failed to begin screenshot command buffer"));
  }

  VkImageSubresourceRange const color_range{
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  // After a normal frame the color target is already TRANSFER_SRC_OPTIMAL.
  auto to_src = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.color_image, color_range);
  to_src.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  context.disp.cmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &to_src);

  VkBufferImageCopy const region{
    .bufferOffset = 0,
    .bufferRowLength = 0,
    .bufferImageHeight = 0,
    .imageSubresource =
      {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    .imageOffset = { .x = 0, .y = 0, .z = 0 },
    .imageExtent = { .width = data.color_width, .height = data.color_height, .depth = 1 },
  };
  context.disp.cmdCopyImageToBuffer(
    cmd, data.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->handle, 1, &region);

  if (context.disp.endCommandBuffer(cmd) != VK_SUCCESS) {
    return fail(MakeError(std::errc::io_error, "failed to end screenshot command buffer"));
  }

  if (data.frame_timeline.has_value()) {
    auto const signal_value = data.next_timeline_value + 1U;
    data.next_timeline_value = signal_value;
    std::array<VkSemaphore, 1> signal_semaphores{ data.frame_timeline->handle() };
    std::array<u64, 1> const signal_values{ signal_value };
    auto timeline_submit = initializers::TimelineSemaphoreSubmitInfo({}, signal_values);
    VkSubmitInfo const submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = &timeline_submit,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = nullptr,
      .pWaitDstStageMask = nullptr,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = signal_semaphores.data(),
    };
    if (context.disp.queueSubmit(data.graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
      return fail(MakeError(std::errc::io_error, "failed to submit screenshot readback"));
    }
    if (auto waited = data.frame_timeline->wait_value(signal_value); !waited) {
      return fail(waited.error());
    }
  } else {
    VkSubmitInfo const submit_info{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = nullptr,
      .waitSemaphoreCount = 0,
      .pWaitSemaphores = nullptr,
      .pWaitDstStageMask = nullptr,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd,
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = nullptr,
    };
    if (context.disp.queueSubmit(data.graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
      return fail(MakeError(std::errc::io_error, "failed to submit screenshot readback"));
    }
    context.disp.deviceWaitIdle();
  }
  FreeCmd(context, data, cmd);
  cmd = VK_NULL_HANDLE;

  context.gpu_allocator.invalidate_buffer(*staging);
  auto mapped = context.gpu_allocator.map_buffer(*staging);
  if (!mapped) {
    DestroyStaging(context, *staging);
    return std::unexpected{ mapped.error() };
  }

  auto const pixels = std::span<std::uint8_t const>{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<std::uint8_t const *>(mapped->data()),
    static_cast<std::size_t>(byte_size),
  };
  auto const written = WritePng(path, data.color_width, data.color_height, kChannels, pixels);
  context.gpu_allocator.unmap_buffer(*staging);
  DestroyStaging(context, *staging);
  return written;
}

}// namespace vkgsplat::vulkan
