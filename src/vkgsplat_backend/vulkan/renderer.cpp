#include "vulkan/renderer.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <system_error>
#include <utility>

#include "app_state.hpp"
#include "vulkan/command/pool.hpp"
#include "vulkan/descriptor/descriptor_heap.hpp"
#include "vulkan/graphics_pipeline.hpp"
#include "vulkan/gs/binning.hpp"
#include "vulkan/gs/projection.hpp"
#include "vulkan/gs/sorting.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "vulkan/sphere_setup.hpp"
#include "vulkan/sync_objects/semaphore.hpp"
#include "vulkan/vulkan_bootstrap.hpp"
#include "gs/pipeline.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "gs/rasterization.hpp"
#include "mesh_gpu.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

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
  data.available_semaphores.clear();
  data.finished_semaphore.clear();
  data.frame_timeline.reset();
  data.next_timeline_value = 0;
  data.slot_timeline_value.fill(0);
  data.image_timeline_value.assign(context.swapchain->image_count(), 0);

  data.available_semaphores.reserve(kFrameSlotCount);
  data.finished_semaphore.reserve(context.swapchain->image_count());

  for (size_t i = 0; i < context.swapchain->image_count(); i++) {
    auto semaphore = Semaphore::create(std::ref(context.disp));
    if (!semaphore) { return std::unexpected{ semaphore.error() }; }
    data.finished_semaphore.push_back(std::move(*semaphore));
  }

  for (size_t i = 0; i < kFrameSlotCount; i++) {
    auto available = Semaphore::create(std::ref(context.disp));
    if (!available) { return std::unexpected{ available.error() }; }
    data.available_semaphores.push_back(std::move(*available));
  }

  auto timeline = Semaphore::create_timeline(std::ref(context.disp));
  if (!timeline) { return std::unexpected{ timeline.error() }; }
  data.frame_timeline = std::move(*timeline);

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
  if (auto recreated = context.swapchain->recreate(context.device, context.platform->framebuffer_extent()); !recreated) {
    return std::unexpected{ recreated.error() };
  }

  // Present wait semaphores are per swapchain image; recreate after idle resize.
  data.finished_semaphore.clear();
  data.finished_semaphore.reserve(context.swapchain->image_count());
  for (size_t i = 0; i < context.swapchain->image_count(); ++i) {
    auto semaphore = Semaphore::create(std::ref(context.disp));
    if (!semaphore) { return std::unexpected{ semaphore.error() }; }
    data.finished_semaphore.push_back(std::move(*semaphore));
  }
  data.image_timeline_value.assign(context.swapchain->image_count(), 0);
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

  data.available_semaphores.clear();
  data.finished_semaphore.clear();
  data.frame_timeline.reset();
  data.next_timeline_value = 0;
  data.slot_timeline_value.fill(0);
  data.image_timeline_value.clear();

  data.command_buffers.clear();
  data.command_pool.reset();

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
