#include "backend/vulkan/renderer.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <print>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include "app_state.hpp"
#include "backend/vulkan/command/command.hpp"
#include "backend/vulkan/command/pool.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "backend/vulkan/gpu_pass_timer.hpp"
#include "backend/vulkan/graphics_pipeline.hpp"
#include "backend/vulkan/imgui_overlay.hpp"
#include "backend/vulkan/initializers.hpp"
#include "backend/vulkan/vulkan_bootstrap.hpp"
#include "gs/binning.hpp"
#include "gs/pipeline.hpp"
#include "gs/projection.hpp"
#include "gs/rasterization.hpp"
#include "gs/sorting.hpp"
#include "mesh_gpu.hpp"
#include "sphere_setup.hpp"
#include "sync_objects/fence.hpp"
#include "sync_objects/semaphore.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {


namespace {

  void RecordSphereDraw(Init &init,
    RenderData &data,
    Camera const &camera,
    f64 aspect_ratio,
    VkCommandBuffer command_buffer,
    size_t image_index)
  {
    BindDescriptorHeap(init, data, command_buffer);

    gs::UpdateGsFrameState(
      init, data, { .camera = &camera, .image_index = image_index, .aspect_ratio = aspect_ratio });
    gs::EvalGsPipeline(init, data, command_buffer);
    RecordImguiOverlay(init, data, command_buffer, image_index);
  }

}// namespace

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

auto DrawFrame(Init &init, RenderData &data, Camera const &camera) -> std::expected<void, Error>
{
  auto const aspect_ratio =
    static_cast<f64>(init.swapchain->extent().width) / static_cast<f64>(init.swapchain->extent().height);

  auto *in_flight_fence = data.in_flight_fences.at(data.current_frame).handle();
  init.disp.waitForFences(1, &in_flight_fence, VK_TRUE, UINT64_MAX);

  if (data.gpu_pass_timer.enabled()) {
    data.gpu_pass_timer.resolve(init, data.current_frame);
    if (data.imgui != nullptr) {
      UpdateImguiGpuTimings(data);
    } else {
      static auto last_print = std::chrono::steady_clock::time_point{};
      auto const now = std::chrono::steady_clock::now();
      if (last_print.time_since_epoch().count() == 0 || now - last_print >= std::chrono::seconds{ 1 }) {
        last_print = now;
        auto const &pass_ms = data.gpu_pass_timer.last_ms();
        std::println("GPU: {:.2f} ms (proj {:.2f} bin {:.2f} prep {:.2f} radix {:.2f} raster {:.2f})",
          data.gpu_pass_timer.total_ms(),
          pass_ms.at(static_cast<size_t>(GpuPass::kProjection)),
          pass_ms.at(static_cast<size_t>(GpuPass::kBinning)),
          pass_ms.at(static_cast<size_t>(GpuPass::kPrepareSort)),
          pass_ms.at(static_cast<size_t>(GpuPass::kRadixSort)),
          pass_ms.at(static_cast<size_t>(GpuPass::kRasterize)));
      }
    }
  }

  uint32_t image_index = 0;
  auto *available_semaphore = data.available_semaphores.at(data.current_frame).handle();
  VkResult result = init.disp.acquireNextImageKHR(
    init.swapchain->handle(), UINT64_MAX, available_semaphore, VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return RecreateSwapchain(init, data);
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return std::unexpected{ MakeError(
      std::errc::io_error, "failed to acquire swapchain image. VkResult=" + std::to_string(result)) };
  }

  if (data.image_in_flight.at(image_index) != VK_NULL_HANDLE) {
    init.disp.waitForFences(1, &data.image_in_flight.at(image_index), VK_TRUE, UINT64_MAX);
  }
  data.image_in_flight.at(image_index) = in_flight_fence;

  auto recorded = vulkan::WithCommand(std::ref(init.disp),
    data.command_buffers.at(image_index).handle(),
    [&](vkb::DispatchTable &, VkCommandBuffer cmd) -> void {
      RecordSphereDraw(init, data, camera, aspect_ratio, cmd, image_index);
    });
  if (!recorded) { return std::unexpected{ recorded.error() }; }

  std::array<VkSemaphore, 1> wait_semaphores = { available_semaphore };
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  auto *finished_semaphore = data.finished_semaphore.at(image_index).handle();
  std::array<VkSemaphore, 1> signal_semaphores = { finished_semaphore };

  auto *command_buffer = data.command_buffers.at(image_index).handle();
  auto const submit_info =
    initializers::SubmitInfo(wait_semaphores, wait_stages, std::span{ &command_buffer, 1 }, signal_semaphores);

  init.disp.resetFences(1, &in_flight_fence);

  if (init.disp.queueSubmit(data.graphics_queue, 1, &submit_info, in_flight_fence) != VK_SUCCESS) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to submit draw command buffer") };
  }

  std::array<VkSwapchainKHR, 1> const swap_chains = { init.swapchain->handle() };
  auto const present_info = initializers::PresentInfoKHR(signal_semaphores, swap_chains, std::span{ &image_index, 1 });

  result = init.disp.queuePresentKHR(data.present_queue, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return RecreateSwapchain(init, data);
  } else if (result != VK_SUCCESS) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to present swapchain image") };
  }

  data.current_frame = (data.current_frame + 1) % kMaxFramesInFlight;
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
