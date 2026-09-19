#include "screenshot.hpp"

#include "app_state.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_io/write_png.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vkexec/barrier.hpp>
#include <vkexec/gpu_buffer.hpp>
#include <vkexec/queue_submit.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

namespace {

  void FreeCmd(Context &context, RenderData &data, VkCommandBuffer cmd)
  {
    if (cmd != VK_NULL_HANDLE && data.command_pool.has_value()) {
      context.disp.freeCommandBuffers(data.command_pool->handle(), 1, &cmd);
    }
  }

}// namespace

auto SaveColorTargetPng(Context &context, RenderData &data, std::string_view path) -> std::expected<void, Error>
{
  if (!data.color_image.has_value() || data.color_width == 0 || data.color_height == 0) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "no color target available for screenshot") };
  }
  if (data.color_format != VK_FORMAT_R8G8B8A8_UNORM) {
    return std::unexpected{ MakeError(std::errc::not_supported, "screenshot currently requires R8G8B8A8_UNORM") };
  }
  if (!data.command_pool.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "command pool missing for screenshot") };
  }
  if (context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "vkexec context missing for screenshot") };
  }
  if (!data.frame_ring.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "frame_ring missing for screenshot") };
  }

  auto &ring = *data.frame_ring;
  for (std::size_t slot = 0; slot < ring.slot_count(); ++slot) {
    if (auto waited = ring.wait_slot(slot); !waited) {
      return std::unexpected{ MakeError(std::errc::io_error, waited.error().message()) };
    }
  }

  constexpr u32 kChannels = 4;
  auto const byte_size = static_cast<VkDeviceSize>(data.color_width) * data.color_height * kChannels;
  auto staging_created = CreateStagingBuffer(*context.vkexec_context, byte_size);
  if (!staging_created) { return std::unexpected{ staging_created.error() }; }
  auto staging = std::optional<vkexec::gpu_buffer>{ std::move(*staging_created) };

  auto buffers = data.command_pool->allocate_buffers(1);
  if (!buffers) { return std::unexpected{ buffers.error() }; }
  VkCommandBuffer cmd = buffers->front().handle();

  auto fail = [&](Error err) -> std::expected<void, Error> {
    FreeCmd(context, data, cmd);
    staging.reset();
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

  // After a normal frame the color target is already TRANSFER_SRC_OPTIMAL.
  vkexec::image_barrier(cmd,
    {
      .image = data.color_image->handle(),
      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .src_access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dst_access = VK_ACCESS_TRANSFER_READ_BIT,
    });

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
    cmd, data.color_image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging->handle(), 1, &region);

  if (context.disp.endCommandBuffer(cmd) != VK_SUCCESS) {
    return fail(MakeError(std::errc::io_error, "failed to end screenshot command buffer"));
  }

  auto const signal_value = ring.allocate_signal_value();
  std::array<vkexec::semaphore_submit, 1> const signals{ vkexec::semaphore_submit{
    .semaphore = ring.timeline().handle(),
    .value = signal_value,
    .stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
  } };
  std::array<VkCommandBuffer, 1> const cmds{ cmd };
  if (auto submitted = context.vkexec_context->submit(vkexec::queue_submit{
        .command_buffers = cmds,
        .signals = signals,
        .queue = data.graphics_queue,
      });
    !submitted) {
    return fail(MakeError(std::errc::io_error, submitted.error().message()));
  }
  if (auto waited = ring.timeline().wait(signal_value); !waited) {
    return fail(MakeError(std::errc::io_error, waited.error().message()));
  }

  FreeCmd(context, data, cmd);
  cmd = VK_NULL_HANDLE;

  auto const mapped = staging->mapped();
  if (mapped.empty()) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to map screenshot staging buffer") };
  }

  auto const pixels = std::span<std::uint8_t const>{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<std::uint8_t const *>(mapped.data()),
    static_cast<std::size_t>(byte_size),
  };
  auto const written = WritePng(path, data.color_width, data.color_height, kChannels, pixels);
  staging.reset();
  return written;
}

}// namespace vkgsplat::vulkan
