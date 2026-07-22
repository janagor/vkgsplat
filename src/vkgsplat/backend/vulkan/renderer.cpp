#include "backend/vulkan/renderer.hpp"

#include <algorithm>
#include <array>
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
#include "backend/vulkan/depth_buffer.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "backend/vulkan/graphics_pipeline.hpp"
#include "backend/vulkan/initializers.hpp"
#include "backend/vulkan/vulkan_bootstrap.hpp"
#include "gs/binning.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/pipeline.hpp"
#include "gs/projection.hpp"
#include "gs/rasterization.hpp"
#include "gs/sorting.hpp"
#include "mesh_gpu.hpp"
#include "sphere_setup.hpp"
#include "sync_objects/fence.hpp"
#include "sync_objects/semaphore.hpp"
#include "vulkan_context.hpp"
#include "window.hpp"
#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

using namespace gs;

namespace {

  void record_sphere_draw(Init &init,
    RenderData &data,
    Camera const &camera,
    f64 aspect_ratio,
    VkCommandBuffer command_buffer,
    size_t image_index)
  {
    bind_descriptor_heap(init, data, command_buffer);
    data.compute_sequence.eval(init, data, command_buffer);

    update_gs_frame_state(init, data, { .camera = camera, .image_index = image_index, .aspect_ratio = aspect_ratio });
    eval_gs_pipeline(init, data, command_buffer);
  }

}// namespace

namespace {

  [[nodiscard]] auto make_error(std::errc errc_value, std::string message) -> Error
  { return Error{ std::make_error_code(errc_value), std::move(message) }; }

  void debug_log_raster_state(Init &init, RenderData const &data)
  {
    static int debug_frames = 0;
    if (debug_frames >= 2) { return; }

    init.disp.queueWaitIdle(data.graphics_queue);
    auto instance_count = init.gpu_allocator.read_buffer<u32>(data.instance_count_buffer, 1);
    auto projected = init.gpu_allocator.read_buffer<GaussianProjected>(data.projected_buffer, data.splat_count);
    auto ranges = init.gpu_allocator.read_buffer<TileRange>(data.tile_ranges_buffer, data.tile_count);
    if (!instance_count || !projected || !ranges) {
      std::println(stderr, "[raster debug] failed to read back GPU buffers");
      ++debug_frames;
      return;
    }

    u32 const live_radii = static_cast<u32>(std::count_if(projected->begin(),
      projected->end(),
      [](GaussianProjected const &projected_splat) { return projected_splat.radius >= 1.0F; }));
    u32 const nonempty_tiles = static_cast<u32>(std::count_if(
      ranges->begin(), ranges->end(), [](TileRange const &tile_range) { return tile_range.end > tile_range.start; }));

    std::println(stderr,
      "[raster debug] instances={} live_radii={}/{} nonempty_tiles={} sort_size={} viewport={}x{}",
      instance_count->at(0),
      live_radii,
      data.splat_count,
      nonempty_tiles,
      data.gaussian_sort_size,
      data.color_width,
      data.color_height);

    for (auto const &candidate : *projected) {
      if (candidate.radius < 1.0F) { continue; }
      std::println(stderr,
        "[raster debug] sample mean=({:.1f},{:.1f}) depth={:.3f} radius={:.1f} conic=({:.4f},{:.4f},{:.4f})",
        candidate.screen_position.at(0),
        candidate.screen_position.at(1),
        candidate.depth,
        candidate.radius,
        candidate.conic.at(0),
        candidate.conic.at(1),
        candidate.conic.at(2));

      u32 const tile_size = k_tile_size;
      u32 const tiles_x = (data.color_width + tile_size - 1U) / tile_size;
      auto const mean_x = static_cast<u32>(candidate.screen_position.at(0));
      auto const mean_y = static_cast<u32>(candidate.screen_position.at(1));
      u32 const tile_id = ((mean_y / tile_size) * tiles_x) + (mean_x / tile_size);
      if (tile_id < ranges->size()) {
        auto const &tile_range = ranges->at(tile_id);
        std::println(stderr,
          "[raster debug] tile_id={} range=[{}, {}) tiles_x={}",
          tile_id,
          tile_range.start,
          tile_range.end,
          tiles_x);
      }

      auto sorted_values = init.gpu_allocator.read_buffer<u32>(data.sorted_values_buffer, data.gaussian_sort_size);
      if (sorted_values && tile_id < ranges->size()) {
        auto const &tile_range = ranges->at(tile_id);
        if (tile_range.end > tile_range.start && tile_range.start < sorted_values->size()) {
          u32 const gaussian_id = sorted_values->at(tile_range.start);
          std::println(stderr, "[raster debug] first gaussian in tile={}", gaussian_id);
        }
      }
      break;
    }
    ++debug_frames;
  }

}// namespace

auto get_queues(Init &init, RenderData &data) -> std::expected<void, Error>
{
  return VKBResultToExpected(init.device.get_queue(vkb::QueueType::graphics))
    .and_then([&](auto const &graphics_queue) {
      data.graphics_queue = graphics_queue;
      return VKBResultToExpected(init.device.get_queue(vkb::QueueType::present));
    })
    .and_then([&](auto const &present_queue) {
      data.present_queue = present_queue;
      return std::expected<void, Error>{};
    });
}

auto create_command_resources(Init &init, RenderData &data) -> std::expected<void, Error>
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

auto create_sync_objects(Init &init, RenderData &data) -> std::expected<void, Error>
{
  data.available_semaphores.clear();
  data.finished_semaphore.clear();
  data.in_flight_fences.clear();
  data.image_in_flight.assign(init.swapchain->image_count(), VK_NULL_HANDLE);

  data.available_semaphores.reserve(k_max_frames_in_flight);
  data.finished_semaphore.reserve(init.swapchain->image_count());
  data.in_flight_fences.reserve(k_max_frames_in_flight);

  for (size_t i = 0; i < init.swapchain->image_count(); i++) {
    auto semaphore = Semaphore::create(std::ref(init.disp));
    if (!semaphore) { return std::unexpected{ semaphore.error() }; }
    data.finished_semaphore.push_back(std::move(*semaphore));
  }

  for (size_t i = 0; i < k_max_frames_in_flight; i++) {
    auto available = Semaphore::create(std::ref(init.disp));
    if (!available) { return std::unexpected{ available.error() }; }
    data.available_semaphores.push_back(std::move(*available));

    auto fence = Fence::create(std::ref(init.disp), VK_FENCE_CREATE_SIGNALED_BIT);
    if (!fence) { return std::unexpected{ fence.error() }; }
    data.in_flight_fences.push_back(std::move(*fence));
  }
  return {};
}

auto recreate_swapchain(Init &init, RenderData &data) -> std::expected<void, Error>
{
  init.disp.deviceWaitIdle();

  data.command_buffers.clear();
  data.command_pool.reset();

  destroy_graphics_pipeline(init, data);

  destroy_depth_buffer(init, data);

  if (init.swapchain == nullptr) {
    return std::unexpected{ make_error(std::errc::state_not_recoverable, "swapchain is not initialized") };
  }
  if (auto recreated = init.swapchain->recreate(init.device, init.window); !recreated) {
    return std::unexpected{ recreated.error() };
  }
  if (0 != create_graphics_pipeline(init, data)) {
    return std::unexpected{ make_error(std::errc::io_error, "failed to recreate graphics pipeline") };
  }
  if (!create_depth_buffer(init, data)) {
    return std::unexpected{ make_error(std::errc::io_error, "failed to recreate depth buffer") };
  }
  if (!recreate_rasterization_color_target(init, data)) {
    return std::unexpected{ make_error(std::errc::io_error, "failed to recreate rasterize color target") };
  }
  if (auto command_resources = create_command_resources(init, data); !command_resources) {
    return std::unexpected{ command_resources.error() };
  }
  return {};
}

auto draw_frame(Init &init, RenderData &data, Camera const &camera) -> std::expected<void, Error>
{
  auto const aspect_ratio =
    static_cast<f64>(init.swapchain->extent().width) / static_cast<f64>(init.swapchain->extent().height);

  auto *in_flight_fence = data.in_flight_fences.at(data.current_frame).handle();
  init.disp.waitForFences(1, &in_flight_fence, VK_TRUE, UINT64_MAX);

  uint32_t image_index = 0;
  auto *available_semaphore = data.available_semaphores.at(data.current_frame).handle();
  VkResult result = init.disp.acquireNextImageKHR(
    init.swapchain->handle(), UINT64_MAX, available_semaphore, VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return recreate_swapchain(init, data);
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return std::unexpected{ make_error(
      std::errc::io_error, "failed to acquire swapchain image. VkResult=" + std::to_string(result)) };
  }

  if (data.image_in_flight.at(image_index) != VK_NULL_HANDLE) {
    init.disp.waitForFences(1, &data.image_in_flight.at(image_index), VK_TRUE, UINT64_MAX);
  }
  data.image_in_flight.at(image_index) = in_flight_fence;

  auto recorded = vulkan::with_command(
    std::ref(init.disp), data.command_buffers.at(image_index).handle(), [&](vkb::DispatchTable &, VkCommandBuffer cmd) {
      record_sphere_draw(init, data, camera, aspect_ratio, cmd, image_index);
    });
  if (!recorded) { return std::unexpected{ recorded.error() }; }

  std::array<VkSemaphore, 1> wait_semaphores = { available_semaphore };
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
  auto *finished_semaphore = data.finished_semaphore.at(image_index).handle();
  std::array<VkSemaphore, 1> signal_semaphores = { finished_semaphore };

  auto *command_buffer = data.command_buffers.at(image_index).handle();
  auto const submit_info =
    initializers::SubmitInfo(wait_semaphores, wait_stages, std::span{ &command_buffer, 1 }, signal_semaphores);

  init.disp.resetFences(1, &in_flight_fence);

  if (init.disp.queueSubmit(data.graphics_queue, 1, &submit_info, in_flight_fence) != VK_SUCCESS) {
    return std::unexpected{ make_error(std::errc::io_error, "failed to submit draw command buffer") };
  }

  debug_log_raster_state(init, data);

  std::array<VkSwapchainKHR, 1> const swap_chains = { init.swapchain->handle() };
  auto const present_info = initializers::PresentInfoKHR(signal_semaphores, swap_chains, std::span{ &image_index, 1 });

  result = init.disp.queuePresentKHR(data.present_queue, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return recreate_swapchain(init, data);
  } else if (result != VK_SUCCESS) {
    return std::unexpected{ make_error(std::errc::io_error, "failed to present swapchain image") };
  }

  data.current_frame = (data.current_frame + 1) % k_max_frames_in_flight;
  return {};
}

void cleanup(Init &init, RenderData &data)
{
  data.available_semaphores.clear();
  data.finished_semaphore.clear();
  data.in_flight_fences.clear();

  data.command_buffers.clear();
  data.command_pool.reset();

  destroy_depth_buffer(init, data);
  destroy_gs_pipeline(data);
  destroy_sphere_buffers(init, data);
  destroy_rasterization(init, data);
  destroy_sorting(init, data);
  destroy_binning(init, data);
  destroy_projection(init, data);
  destroy_sphere_setup(init, data);
  destroy_descriptor_heap(init, data);

  destroy_graphics_pipeline(init, data);

  init.swapchain.reset();

  vkb::destroy_device(init.device);
  vkb::destroy_surface(init.instance, init.surface);
  vkb::destroy_instance(init.instance);
  destroy_window_glfw(init.window);
}

}// namespace vkgsplat
