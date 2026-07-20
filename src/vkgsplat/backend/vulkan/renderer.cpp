#include "backend/vulkan/renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iostream>
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
#include "backend/vulkan/rendering.hpp"
#include "gaussian_splat.hpp"
#include "mesh_gpu.hpp"
#include "bin_gaussians.hpp"
#include "project_gaussians.hpp"
#include "sort_gaussians.hpp"
#include "sphere_setup.hpp"
#include "sync_objects/fence.hpp"
#include "sync_objects/semaphore.hpp"
#include <vkgsplat/types.hpp>
#include "backend/vulkan/vulkan_bootstrap.hpp"
#include "window.hpp"

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

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

  VkImageSubresourceRange const color_subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  VkImageSubresourceRange const depth_subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  auto color_barrier = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    init.swapchain->images().at(image_index),
    color_subresource_range);
  color_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  auto depth_barrier = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    data.depth_image,
    depth_subresource_range);
  depth_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  std::array<VkImageMemoryBarrier, 2> barriers = { color_barrier, depth_barrier };
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    static_cast<uint32_t>(barriers.size()),
    barriers.data());

  VkClearValue const clear_color{ { { 0.02F, 0.02F, 0.05F, 1.0F } } };
  VkClearValue const clear_depth{ { { 1.0F, 0.0F } } };

  VkRenderingAttachmentInfo const color_attachment = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = init.swapchain->image_views().at(image_index),
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .resolveMode = VK_RESOLVE_MODE_NONE,
    .resolveImageView = VK_NULL_HANDLE,
    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = clear_color,
  };

  VkRenderingAttachmentInfo const depth_attachment = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = data.depth_image_view,
    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    .resolveMode = VK_RESOLVE_MODE_NONE,
    .resolveImageView = VK_NULL_HANDLE,
    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .clearValue = clear_depth,
  };

  VkRect2D const render_area{ .offset = { .x = 0, .y = 0 }, .extent = init.swapchain->extent() };
  VkRenderingInfo const rendering_info = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .pNext = nullptr,
    .flags = 0,
    .renderArea = render_area,
    .layerCount = 1,
    .viewMask = 0,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment,
    .pDepthAttachment = &depth_attachment,
    .pStencilAttachment = nullptr,
  };

  VkViewport viewport = {};
  viewport.x = 0.0F;
  viewport.y = 0.0F;
  viewport.width = static_cast<float>(init.swapchain->extent().width);
  viewport.height = static_cast<float>(init.swapchain->extent().height);
  viewport.minDepth = 0.0F;
  viewport.maxDepth = 1.0F;

  VkRect2D scissor = {};
  scissor.offset = { .x = 0, .y = 0 };
  scissor.extent = init.swapchain->extent();

  vulkan::with_rendering(std::ref(init.disp),
    command_buffer,
    rendering_info,
    [&](vkb::DispatchTable &disp, VkCommandBuffer cmd) {
      CameraPushConstants const push_constants{
        .view = camera.view_matrix(),
        .projection = camera.projection_matrix(aspect_ratio),
      };

      disp.cmdSetViewport(cmd, 0, 1, &viewport);
      disp.cmdSetScissor(cmd, 0, 1, &scissor);
      disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);

      VkPushDataInfoEXT const push_info = {
        .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
        .pNext = nullptr,
        .offset = 0,
        .data = { .address = &push_constants, .size = sizeof(CameraPushConstants) },
      };
      init.cmd_push_data(cmd, &push_info);

      disp.cmdDraw(cmd, k_verts_per_sphere, data.splat_count, 0, 0);
    });

  auto present_barrier = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    init.swapchain->images().at(image_index),
    color_subresource_range);
  present_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &present_barrier);
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
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
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
