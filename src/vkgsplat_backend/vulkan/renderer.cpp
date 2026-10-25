#include "vulkan/renderer.hpp"

#include <expected>
#include <functional>
#include <system_error>
#include <utility>

#include "app_state.hpp"
#include "gs/pipeline.hpp"
#include "gs/rasterization.hpp"
#include "mesh_gpu.hpp"
#include "vulkan/command/pool.hpp"
#include "vulkan/descriptor/descriptor_heap.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "vulkan/gs/graphics/graphics_pipeline.hpp"
#include "vulkan/gs/graphics/binning.hpp"
#include "vulkan/gs/projection.hpp"
#include "vulkan/gs/graphics/sorting.hpp"
#include "vulkan/gs/tiled_compute/operations.hpp"
#include "vulkan/gs/tiled_compute/resources.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "vulkan/sphere_setup.hpp"
#include "vulkan/vulkan_bootstrap.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vkexec/sync_wait.hpp>
#include <vkexec_extensions/timeline_semaphore/frame_ring.hpp>
#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

auto GetQueues(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  auto graphics = VKBResultToExpected(context.device.get_queue(vkb::QueueType::graphics));
  if (!graphics) { return std::unexpected(graphics.error()); }
  data.graphics_queue = *graphics;
  if (context.swapchain == nullptr) { return {}; }
  auto present = VKBResultToExpected(context.device.get_queue(vkb::QueueType::present));
  if (!present) { return std::unexpected(present.error()); }
  data.present_queue = *present;
  return {};
}

auto CreateCommandResources(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  data.command_buffers.clear();
  data.command_pool.reset();

  auto pool = vulkan::CommandPool::create(std::ref(context.disp),
    static_cast<u32>(context.device.get_queue_index(vkb::QueueType::graphics).value()),
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  if (!pool) { return std::unexpected{ pool.error() }; }
  data.command_pool = std::move(*pool);

  u32 const buffer_count = context.swapchain == nullptr ? static_cast<u32>(kFrameSlotCount)
                                                        : static_cast<u32>(context.swapchain->image_views().size());
  auto buffers = data.command_pool->allocate_buffers(buffer_count);
  if (!buffers) { return std::unexpected{ buffers.error() }; }
  data.command_buffers = std::move(*buffers);
  return {};
}

auto CreateSyncObjects(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  data.frame_ring.reset();

  if (context.swapchain == nullptr) {
    VkSemaphoreTypeCreateInfo const timeline_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .pNext = nullptr,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = 0U,
    };
    VkSemaphoreCreateInfo const create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &timeline_info,
      .flags = 0U,
    };
    if (context.disp.createSemaphore(&create_info, nullptr, &data.offscreen_timeline) != VK_SUCCESS) {
      return std::unexpected{ MakeError(std::errc::io_error, "failed to create offscreen timeline semaphore") };
    }
    return {};
  }

  if (context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "vkexec context missing for frame_ring") };
  }

  auto created = vkexec::try_sync_wait_value(vkexec::factory::make_frame_ring(*context.vkexec_context,
    vkexec::frame_ring_create_info{
      .slot_count = kFrameSlotCount,
      .image_count = context.swapchain->image_count(),
    }));
  if (!created) { return std::unexpected{ MakeError(std::errc::io_error, created.error().message()) }; }
  data.frame_ring = std::move(*created);
  return {};
}

auto RecreateSwapchain(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  auto const extent = context.platform->framebuffer_extent();
  if (context.disp.deviceWaitIdle() != VK_SUCCESS) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to wait for device before swapchain recreation") };
  }
  // Submission completion does not cover a pending presentation wait.
  if (context.disp.queueWaitIdle(data.present_queue) != VK_SUCCESS) {
    return std::unexpected{ MakeError(
      std::errc::io_error, "failed to wait for presentation before swapchain recreation") };
  }
  if (data.tiled_status_pending) {
    auto status = gs::tiled::ReadFrameStatus(data);
    data.tiled_status_pending = false;
    if (!status) { return status; }
  }
  if (extent.width == 0 || extent.height == 0) { return CreateSyncObjects(context, data); }

  data.in_flight_readbacks.clear();
  data.command_buffers.clear();
  data.command_pool.reset();

  if (data.rasterization_mode == RasterizationMode::kGraphicsPipeline) { DestroyGraphicsPipeline(context, data); }

  if (context.swapchain == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "swapchain is not initialized") };
  }
  if (auto recreated = context.swapchain->recreate(context.device, extent); !recreated) {
    return std::unexpected{ recreated.error() };
  }
  auto const actual_extent = context.swapchain->extent();
  data.output_extent = { .width = actual_extent.width, .height = actual_extent.height };

  if (auto sync = CreateSyncObjects(context, data); !sync) { return sync; }

  if (!gs::RecreateRasterizationColorTarget(context, data)) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to recreate rasterize color target") };
  }
  if (data.rasterization_mode == RasterizationMode::kGraphicsPipeline) {
    if (0 != CreateGraphicsPipeline(context, data)) {
      return std::unexpected{ MakeError(std::errc::io_error, "failed to recreate graphics pipeline") };
    }
  } else {
    if (auto resources = gs::tiled::RecreateExtentResources(context, data); !resources) { return resources; }
  }
  if (!RefreshDescriptorHeap(context, data)) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to refresh descriptors after swapchain recreation") };
  }
  if (auto command_resources = CreateCommandResources(context, data); !command_resources) {
    return std::unexpected{ command_resources.error() };
  }
  RecreateImguiOverlayPipeline(context, data);
  if (data.present_pacer != nullptr) { data.present_pacer->OnSwapchainRecreated(context); }
  return {};
}

void Cleanup(vulkan::Context &context, RenderData &data)
{
  context.disp.deviceWaitIdle();
  if (data.present_queue != VK_NULL_HANDLE) { context.disp.queueWaitIdle(data.present_queue); }

  data.frame_ring.reset();
  if (data.offscreen_timeline != VK_NULL_HANDLE) {
    context.disp.destroySemaphore(data.offscreen_timeline, nullptr);
    data.offscreen_timeline = VK_NULL_HANDLE;
  }

  data.in_flight_readbacks.clear();
  data.command_buffers.clear();
  data.command_pool.reset();

  // ImGui callbacks and secondary inheritance borrow the shared heap storage.
  ShutdownImguiOverlay(context, data);

  gs::DestroyGsPipeline(data);
  DestroySphereBuffers(context, data);
  if (data.rasterization_mode == RasterizationMode::kComputeTiled) {
    gs::tiled::DestroyAlgorithms(data);
    gs::tiled::DestroyResources(data);
  } else {
    gs::DestroySorting(context, data);
    gs::DestroyBinning(context, data);
  }
  gs::DestroyRasterization(context, data);
  gs::DestroyProjection(context, data);
  DestroySphereSetup(context, data);
  DestroyDescriptorHeap(context, data);

  data.gpu_pass_timer.destroy(context);

  DestroyGraphicsPipeline(context, data);

  // Swapchain, allocator, and device are owned by VulkanDriver / Engine.
}

}// namespace vkgsplat
