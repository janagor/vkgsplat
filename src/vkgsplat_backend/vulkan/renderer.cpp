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
#include "vulkan/graphics_pipeline.hpp"
#include "vulkan/gs/binning.hpp"
#include "vulkan/gs/projection.hpp"
#include "vulkan/gs/sorting.hpp"
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
  return VKBResultToExpected(context.device.get_queue(vkb::QueueType::graphics))
    .and_then([&](VkQueue const &graphics_queue) -> std::expected<VkQueue, Error> {
      data.graphics_queue = graphics_queue;
      return VKBResultToExpected(context.device.get_queue(vkb::QueueType::present));
    })
    .and_then([&](VkQueue const &present_queue) -> std::expected<void, Error> {
      data.present_queue = present_queue;
      return std::expected<void, Error>{};
    });
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

  auto buffers = data.command_pool->allocate_buffers(static_cast<u32>(context.swapchain->image_views().size()));
  if (!buffers) { return std::unexpected{ buffers.error() }; }
  data.command_buffers = std::move(*buffers);
  return {};
}

auto CreateSyncObjects(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  data.frame_ring.reset();

  if (context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "vkexec context missing for frame_ring") };
  }
  if (context.swapchain == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "swapchain is not initialized") };
  }

  auto created = vkexec::try_sync_wait_value(vkexec::frame_ring::create(*context.vkexec_context,
    vkexec::frame_ring::create_info{
      .slot_count = kFrameSlotCount,
      .image_count = context.swapchain->image_count(),
    }));
  if (!created) { return std::unexpected{ MakeError(std::errc::io_error, created.error().message()) }; }
  data.frame_ring = std::move(*created);
  return {};
}

auto RecreateSwapchain(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  context.disp.deviceWaitIdle();

  data.command_buffers.clear();
  data.command_pool.reset();

  DestroyGraphicsPipeline(context, data);

  if (context.swapchain == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "swapchain is not initialized") };
  }
  if (auto recreated = context.swapchain->recreate(context.device, context.platform->framebuffer_extent());
    !recreated) {
    return std::unexpected{ recreated.error() };
  }

  if (!data.frame_ring.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "frame_ring is not initialized") };
  }
  if (auto resized = data.frame_ring->resize_images(context.swapchain->image_count()); !resized) {
    return std::unexpected{ MakeError(std::errc::io_error, resized.error().message()) };
  }
  data.frame_ring->reset_completion_tracking();

  if (0 != CreateGraphicsPipeline(context, data)) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to recreate graphics pipeline") };
  }
  if (!gs::RecreateRasterizationColorTarget(context, data)) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to recreate rasterize color target") };
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

  data.frame_ring.reset();

  data.command_buffers.clear();
  data.command_pool.reset();

  // ImGui callbacks and secondary inheritance borrow the shared heap storage.
  ShutdownImguiOverlay(context, data);

  gs::DestroyGsPipeline(data);
  DestroySphereBuffers(context, data);
  gs::DestroyRasterization(context, data);
  gs::DestroySorting(context, data);
  gs::DestroyBinning(context, data);
  gs::DestroyProjection(context, data);
  DestroySphereSetup(context, data);
  DestroyDescriptorHeap(context, data);

  data.gpu_pass_timer.destroy(context);

  DestroyGraphicsPipeline(context, data);

  // Swapchain, allocator, and device are owned by VulkanDriver / Engine.
}

}// namespace vkgsplat
