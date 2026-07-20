#include "backend/vulkan/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iostream>
#include <print>
#include <span>
#include <utility>

#include "app_state.hpp"
#include <vkgsplat/camera.hpp>
#include "vulkan_context.hpp"
#include "backend/vulkan/depth_buffer.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include <vkgsplat/error.hpp>
#include "backend/vulkan/graphics_pipeline.hpp"
#include "backend/vulkan/command/command.hpp"
#include "backend/vulkan/command/pool.hpp"
#include "backend/vulkan/initializers.hpp"
#include "3dgs/gaussian_splat.hpp"
#include "3dgs/push_constants.hpp"
#include "mesh_gpu.hpp"
#include "3dgs/bin_gaussians.hpp"
#include "3dgs/project_gaussians.hpp"
#include "3dgs/rasterize_gaussians.hpp"
#include "3dgs/sort_gaussians.hpp"
#include "sphere_setup.hpp"
#include "sync_objects/fence.hpp"
#include "sync_objects/semaphore.hpp"
#include <vkgsplat/types.hpp>
#include "backend/vulkan/vulkan_bootstrap.hpp"
#include "window.hpp"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

using namespace gs;

namespace {

void record_sphere_draw(Init &init,
  RenderData const &data,
  Camera const &camera,
  f64 aspect_ratio,
  VkCommandBuffer command_buffer,
  size_t image_index)
{
  bind_descriptor_heap(init, data, command_buffer);
  dispatch_sphere_setup(init, data, command_buffer);

  ProjectPushConstants const project_push{
    .view = camera.view_matrix(),
    .projection = camera.projection_matrix(aspect_ratio),
    .viewport = { static_cast<float>(init.swapchain->extent().width),
      static_cast<float>(init.swapchain->extent().height) },
    .padding = {},
  };
  dispatch_project_gaussians(init, data, project_push, command_buffer);

  BinPushConstants const bin_push{
    .viewport = { init.swapchain->extent().width, init.swapchain->extent().height },
    .max_instances = data.max_bin_instances,
    .tile_size = k_tile_size,
    .instance_count_address =
      init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer),
  };
  dispatch_bin_gaussians(init, data, bin_push, command_buffer);

  SortPushConstants const sort_push{
    .instance_count_address =
      init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer),
    .sort_size = data.gaussian_sort_size,
    .tile_count = data.tile_count,
  };
  dispatch_sort_gaussians(init, data, sort_push, command_buffer);

  glm::vec3 const camera_pos{ camera.position() };
  RasterPushConstants const raster_push{
    .camera_position = glm::vec4{ camera_pos, 0.0F },
    .viewport = { init.swapchain->extent().width, init.swapchain->extent().height },
    .tile_size = k_tile_size,
    .tiles_x = (init.swapchain->extent().width + k_tile_size - 1U) / k_tile_size,
    .background = { 0.02F, 0.02F, 0.05F, 0.0F },
    .sh_degree = data.procedural ? 0U : 3U,
    .pad0 = 0U,
    .pad1 = 0U,
    .pad2 = 0U,
  };
  dispatch_rasterize_gaussians(init, data, raster_push, command_buffer, image_index);
}

}// namespace

namespace {

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

  u32 const live_radii = static_cast<u32>(std::count_if(projected->begin(), projected->end(), [](GaussianProjected const &projected_splat) {
    return projected_splat.radius >= 1.0F;
  }));
  u32 const nonempty_tiles = static_cast<u32>(std::count_if(ranges->begin(), ranges->end(), [](TileRange const &tile_range) {
    return tile_range.end > tile_range.start;
  }));

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

    auto sorted_values =
      init.gpu_allocator.read_buffer<u32>(data.sorted_values_buffer, data.gaussian_sort_size);
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

auto create_command_resources(Init &init, RenderData &data) -> int
{
  data.command_buffers.clear();
  data.command_pool.reset();

  auto pool = vulkan::CommandPool::create(std::ref(init.disp),
    static_cast<u32>(init.device.get_queue_index(vkb::QueueType::graphics).value()),
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  if (!pool) {
    std::cout << "failed to create command pool\n";
    return -1;
  }
  data.command_pool = std::move(*pool);

  auto buffers = data.command_pool->allocate_buffers(static_cast<u32>(init.swapchain->image_views().size()));
  if (!buffers) { return -1; }
  data.command_buffers = std::move(*buffers);
  return 0;
}

auto create_sync_objects(Init &init, RenderData &data) -> int
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
    if (!semaphore) {
      std::cout << "failed to create sync objects\n";
      return -1;
    }
    data.finished_semaphore.push_back(std::move(*semaphore));
  }

  for (size_t i = 0; i < k_max_frames_in_flight; i++) {
    auto available = Semaphore::create(std::ref(init.disp));
    if (!available) {
      std::cout << "failed to create sync objects\n";
      return -1;
    }
    data.available_semaphores.push_back(std::move(*available));

    auto fence = Fence::create(std::ref(init.disp), VK_FENCE_CREATE_SIGNALED_BIT);
    if (!fence) {
      std::cout << "failed to create sync objects\n";
      return -1;
    }
    data.in_flight_fences.push_back(std::move(*fence));
  }
  return 0;
}

auto recreate_swapchain(Init &init, RenderData &data) -> int
{
  init.disp.deviceWaitIdle();

  data.command_buffers.clear();
  data.command_pool.reset();

  destroy_graphics_pipeline(init, data);

  destroy_depth_buffer(init, data);

  if (init.swapchain == nullptr) { return -1; }
  if (!init.swapchain->recreate(init.device, init.window).has_value()) { return -1; }
  if (0 != create_graphics_pipeline(init, data)) { return -1; }
  if (!create_depth_buffer(init, data)) { return -1; }
  if (!recreate_rasterize_color_target(init, data)) { return -1; }
  if (0 != create_command_resources(init, data)) { return -1; }
  return 0;
}

auto draw_frame(Init &init, RenderData &data, Camera const &camera) -> int
{
  auto const aspect_ratio = static_cast<f64>(init.swapchain->extent().width)
    / static_cast<f64>(init.swapchain->extent().height);

  auto *in_flight_fence = data.in_flight_fences.at(data.current_frame).handle();
  init.disp.waitForFences(1, &in_flight_fence, VK_TRUE, UINT64_MAX);

  uint32_t image_index = 0;
  auto *available_semaphore = data.available_semaphores.at(data.current_frame).handle();
  VkResult result = init.disp.acquireNextImageKHR(
    init.swapchain->handle(), UINT64_MAX, available_semaphore, VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return recreate_swapchain(init, data);
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    std::cout << "failed to acquire swapchain image. Error " << result << "\n";
    return -1;
  }

  if (data.image_in_flight.at(image_index) != VK_NULL_HANDLE) {
    init.disp.waitForFences(1, &data.image_in_flight.at(image_index), VK_TRUE, UINT64_MAX);
  }
  data.image_in_flight.at(image_index) = in_flight_fence;

  auto recorded = vulkan::with_command(std::ref(init.disp),
    data.command_buffers.at(image_index).handle(),
    [&](vkb::DispatchTable &, VkCommandBuffer cmd) {
      record_sphere_draw(init, data, camera, aspect_ratio, cmd, image_index);
    });
  if (!recorded) {
    std::cout << "failed to record command buffer\n";
    return -1;
  }

  std::array<VkSemaphore, 1> wait_semaphores = { available_semaphore };
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };
  auto *finished_semaphore = data.finished_semaphore.at(image_index).handle();
  std::array<VkSemaphore, 1> signal_semaphores = { finished_semaphore };

  auto *command_buffer = data.command_buffers.at(image_index).handle();
  auto const submit_info = initializers::SubmitInfo(
    wait_semaphores, wait_stages, std::span{ &command_buffer, 1 }, signal_semaphores);

  init.disp.resetFences(1, &in_flight_fence);

  if (init.disp.queueSubmit(data.graphics_queue, 1, &submit_info, in_flight_fence) != VK_SUCCESS) {
    std::cout << "failed to submit draw command buffer\n";
    return -1;
  }

  debug_log_raster_state(init, data);

  std::array<VkSwapchainKHR, 1> const swap_chains = { init.swapchain->handle() };
  auto const present_info = initializers::PresentInfoKHR(signal_semaphores, swap_chains, std::span{ &image_index, 1 });

  result = init.disp.queuePresentKHR(data.present_queue, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return recreate_swapchain(init, data);
  } else if (result != VK_SUCCESS) {
    std::cout << "failed to present swapchain image\n";
    return -1;
  }

  data.current_frame = (data.current_frame + 1) % k_max_frames_in_flight;
  return 0;
}

void cleanup(Init &init, RenderData &data)
{
  data.available_semaphores.clear();
  data.finished_semaphore.clear();
  data.in_flight_fences.clear();

  data.command_buffers.clear();
  data.command_pool.reset();

  destroy_depth_buffer(init, data);
  destroy_sphere_buffers(init, data);
  destroy_rasterize_gaussians(init, data);
  destroy_sort_gaussians(init, data);
  destroy_bin_gaussians(init, data);
  destroy_project_gaussians(init, data);
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
