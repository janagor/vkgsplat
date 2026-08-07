#include "backend/vulkan/renderer.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <system_error>
#include <utility>

#include "app_state.hpp"
#include "backend/vulkan/command/pool.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "backend/vulkan/graphics_pipeline.hpp"
#include "backend/vulkan/gs/binning.hpp"
#include "backend/vulkan/gs/projection.hpp"
#include "backend/vulkan/gs/sorting.hpp"
#include "backend/vulkan/imgui_overlay.hpp"
#include "backend/vulkan/sphere_setup.hpp"
#include "backend/vulkan/sync_objects/fence.hpp"
#include "backend/vulkan/sync_objects/semaphore.hpp"
#include "backend/vulkan/vulkan_bootstrap.hpp"
#include "gs/pipeline.hpp"
#include "backend/vulkan/gpu_pass_timer.hpp"
#include "gs/rasterization.hpp"
#include "mesh_gpu.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

auto GetQueues(Init &init, RenderData &data) -> std::expected<void, Error>
{
  return VKBResultToExpected(init.device.get_queue(vkb::QueueType::graphics))
    .and_then([&](VkQueue const &graphics_queue) -> std::expected<VkQueue, Error> {
      data.graphics_queue = graphics_queue;
      return VKBResultToExpected(init.device.get_queue(vkb::QueueType::present));
    })
    .and_then([&](VkQueue const &present_queue) -> std::expected<void, Error> {
      data.present_queue = present_queue;
      return std::expected<void, Error>{};
    });
}

auto CreateCommandResources(Init &init, RenderData &data) -> std::expected<void, Error>
{
  data.command_buffers.clear();
  data.command_pool.reset();

  auto pool = vulkan::CommandPool::create(std::ref(init.disp),
    static_cast<u32>(init.device.get_queue_index(vkb::QueueType::graphics).value()),
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  if (!pool) { return std::unexpected{ pool.error() }; }
  data.command_pool = std::move(*pool);

  auto buffers = data.command_pool->allocate_buffers(static_cast<u32>(init.swapchain->image_views().size()));
  if (!buffers) { return std::unexpected{ buffers.error() }; }
  data.command_buffers = std::move(*buffers);
  return {};
}

auto CreateSyncObjects(Init &init, RenderData &data) -> std::expected<void, Error>
{
  data.available_semaphores.clear();
  data.finished_semaphore.clear();
  data.in_flight_fences.clear();
  data.image_in_flight.assign(init.swapchain->image_count(), VK_NULL_HANDLE);

  data.available_semaphores.reserve(kMaxFramesInFlight);
  data.finished_semaphore.reserve(init.swapchain->image_count());
  data.in_flight_fences.reserve(kMaxFramesInFlight);

  for (size_t i = 0; i < init.swapchain->image_count(); i++) {
    auto semaphore = Semaphore::create(std::ref(init.disp));
    if (!semaphore) { return std::unexpected{ semaphore.error() }; }
    data.finished_semaphore.push_back(std::move(*semaphore));
  }

  for (size_t i = 0; i < kMaxFramesInFlight; i++) {
    auto available = Semaphore::create(std::ref(init.disp));
    if (!available) { return std::unexpected{ available.error() }; }
    data.available_semaphores.push_back(std::move(*available));

    auto fence = Fence::create(std::ref(init.disp), VK_FENCE_CREATE_SIGNALED_BIT);
    if (!fence) { return std::unexpected{ fence.error() }; }
    data.in_flight_fences.push_back(std::move(*fence));
  }
  return {};
}

auto RecreateSwapchain(Init &init, RenderData &data) -> std::expected<void, Error>
{
  init.disp.deviceWaitIdle();

  data.command_buffers.clear();
  data.command_pool.reset();

  DestroyGraphicsPipeline(init, data);

  if (init.swapchain == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "swapchain is not initialized") };
  }
  if (auto recreated = init.swapchain->recreate(init.device, init.platform->framebuffer_extent()); !recreated) {
    return std::unexpected{ recreated.error() };
  }
  if (0 != CreateGraphicsPipeline(init, data)) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to recreate graphics pipeline") };
  }
  if (!gs::RecreateRasterizationColorTarget(init, data)) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to recreate rasterize color target") };
  }
  if (auto command_resources = CreateCommandResources(init, data); !command_resources) {
    return std::unexpected{ command_resources.error() };
  }
  RecreateImguiOverlayPipeline(init, data);
  return {};
}

void Cleanup(Init &init, RenderData &data)
{
  init.disp.deviceWaitIdle();

  data.available_semaphores.clear();
  data.finished_semaphore.clear();
  data.in_flight_fences.clear();

  data.command_buffers.clear();
  data.command_pool.reset();

  ShutdownImguiOverlay(init, data);

  gs::DestroyGsPipeline(data);
  DestroySphereBuffers(init, data);
  gs::DestroyRasterization(init, data);
  gs::DestroySorting(init, data);
  gs::DestroyBinning(init, data);
  gs::DestroyProjection(init, data);
  DestroySphereSetup(init, data);
  DestroyDescriptorHeap(init, data);

  data.gpu_pass_timer.destroy(init);

  DestroyGraphicsPipeline(init, data);

  // Swapchain, allocator, and device are owned by VulkanDriver / Engine.
}

}// namespace vkgsplat
