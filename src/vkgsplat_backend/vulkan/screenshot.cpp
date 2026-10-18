#include "screenshot.hpp"

#include "app_state.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vkexec/barrier.hpp>
#include <vkexec/queue_submit.hpp>
#include <vkexec_vma/gpu_buffer.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

namespace {

  void FreeCmd(Context &context, RenderData &data, VkCommandBuffer cmd)
  {
    if (cmd != VK_NULL_HANDLE && data.command_pool.has_value()) {
      context.disp.freeCommandBuffers(data.command_pool->handle(), 1, &cmd);
    }
  }

  [[nodiscard]] auto WaitForPriorFrames(RenderData &data) -> std::expected<void, Error>
  {
    if (!data.frame_ring.has_value()) { return {}; }
    auto &ring = *data.frame_ring;
    for (std::size_t slot = 0; slot < ring.slot_count(); ++slot) {
      if (auto waited = ring.wait_slot(slot); !waited) {
        return std::unexpected{ MakeError(std::errc::io_error, waited.error().message()) };
      }
    }
    return {};
  }

}// namespace

auto ReadColorTargetRgba8(Context &context, RenderData &data) -> std::expected<RgbaFrame, Error>
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
  if (context.allocator == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "VMA allocator missing for screenshot") };
  }
  if (auto waited = WaitForPriorFrames(data); !waited) { return std::unexpected{ waited.error() }; }

  constexpr u32 kChannels = 4;
  if (data.color_width > std::numeric_limits<size_t>::max() / kChannels / data.color_height) {
    return std::unexpected{ MakeError(std::errc::value_too_large, "color target byte size overflows") };
  }
  auto const byte_size = static_cast<size_t>(data.color_width) * data.color_height * kChannels;
  auto staging_created = CreateStagingBuffer(*context.allocator, byte_size);
  if (!staging_created) { return std::unexpected{ staging_created.error() }; }
  auto staging = std::optional<vulkan::GpuBuffer>{ std::move(*staging_created) };

  auto buffers = data.command_pool->allocate_buffers(1);
  if (!buffers) { return std::unexpected{ buffers.error() }; }
  VkCommandBuffer cmd = buffers->front().handle();

  auto fail = [&](Error err) -> std::expected<RgbaFrame, Error> {
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
  auto barrier = vkexec::image_barrier(*context.vkexec_context,
    cmd,
    {
      .image = data.color_image->handle(),
      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .src_access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dst_access = VK_ACCESS_TRANSFER_READ_BIT,
    });
  if (!barrier) { return fail(MakeError(std::errc::io_error, barrier.error().message())); }

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

  auto *const timeline = data.frame_ring.has_value() ? data.frame_ring->timeline().handle() : data.offscreen_timeline;
  if (timeline == VK_NULL_HANDLE) {
    return fail(MakeError(std::errc::state_not_recoverable, "readback timeline is missing"));
  }
  u64 const signal_value =
    data.frame_ring.has_value() ? data.frame_ring->allocate_signal_value() : ++data.offscreen_signal_value;
  std::array<vkexec::semaphore_submit, 1> const signals{ vkexec::semaphore_submit{
    .semaphore = timeline,
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
    if (!data.frame_ring.has_value()) { --data.offscreen_signal_value; }
    return fail(MakeError(std::errc::io_error, submitted.error().message()));
  }
  VkSemaphoreWaitInfo const wait_info{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .pNext = nullptr,
    .flags = 0U,
    .semaphoreCount = 1U,
    .pSemaphores = &timeline,
    .pValues = &signal_value,
  };
  if (context.disp.waitSemaphores(&wait_info, UINT64_MAX) != VK_SUCCESS) {
    if (context.disp.deviceWaitIdle() != VK_SUCCESS) {
      // Keep the staging allocation and command buffer alive until renderer teardown.
      data.in_flight_readbacks.push_back(std::move(*staging));
      return std::unexpected{ MakeError(std::errc::io_error, "failed to wait for color target readback") };
    }
    return fail(MakeError(std::errc::io_error, "failed to wait for color target readback"));
  }

  FreeCmd(context, data, cmd);
  cmd = VK_NULL_HANDLE;

  auto const mapped = staging->mapped();
  if (mapped.empty()) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to map screenshot staging buffer") };
  }
  if (auto invalidated = staging->invalidate(); !invalidated) {
    return std::unexpected{ MakeError(std::errc::io_error, invalidated.error().message()) };
  }

  auto const pixels = std::span<std::uint8_t const>{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<std::uint8_t const *>(mapped.data()),
    static_cast<std::size_t>(byte_size),
  };
  RgbaFrame frame{ .width = data.color_width,
    .height = data.color_height,
    .pixels = std::vector<std::uint8_t>{ pixels.begin(), pixels.end() } };
  staging.reset();
  return frame;
}

}// namespace vkgsplat::vulkan
