#include "renderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>

#include "app_state.hpp"
#include "descriptor_heap.hpp"
#include "error.hpp"
#include "initializers.hpp"
#include "mesh_gpu.hpp"
#include "swapchain.hpp"
#include "triangle_sort.hpp"
#include "types.hpp"
#include "vulkan_bootstrap.hpp"
#include "window.hpp"

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

namespace {

void record_triangle_draw(Init const &init, RenderData const &data, VkCommandBuffer command_buffer, size_t image_index)
{
  dispatch_triangle_sort(init, data, command_buffer);

  VkImageSubresourceRange const color_subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  auto color_barrier = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    data.swapchain_images.at(image_index),
    color_subresource_range);
  color_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &color_barrier);

  VkClearValue const clear_color{ { { 0.0F, 0.0F, 0.0F, 1.0F } } };
  VkRenderingAttachmentInfo const color_attachment = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = data.swapchain_image_views.at(image_index),
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .resolveMode = VK_RESOLVE_MODE_NONE,
    .resolveImageView = VK_NULL_HANDLE,
    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = clear_color,
  };

  VkRect2D const render_area{ .offset = { .x = 0, .y = 0 }, .extent = init.swapchain.extent };
  VkRenderingInfo const rendering_info = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .pNext = nullptr,
    .flags = 0,
    .renderArea = render_area,
    .layerCount = 1,
    .viewMask = 0,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment,
    .pDepthAttachment = nullptr,
    .pStencilAttachment = nullptr,
  };

  VkViewport viewport = {};
  viewport.x = 0.0F;
  viewport.y = 0.0F;
  viewport.width = static_cast<float>(init.swapchain.extent.width);
  viewport.height = static_cast<float>(init.swapchain.extent.height);
  viewport.minDepth = 0.0F;
  viewport.maxDepth = 1.0F;

  VkRect2D scissor = {};
  scissor.offset = { .x = 0, .y = 0 };
  scissor.extent = init.swapchain.extent;

  init.disp.cmdBeginRendering(command_buffer, &rendering_info);
  init.disp.cmdSetViewport(command_buffer, 0, 1, &viewport);
  init.disp.cmdSetScissor(command_buffer, 0, 1, &scissor);
  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);
  bind_mesh_descriptor_heap(init, data, command_buffer);

  auto const vertex_count = data.mesh.draw_vertex_count();
  if (vertex_count >= 3) { init.disp.cmdDraw(command_buffer, vertex_count, 1, 0, 0); }

  init.disp.cmdEndRendering(command_buffer);

  auto present_barrier = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    data.swapchain_images.at(image_index),
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

auto create_command_pool(Init &init, RenderData &data) -> int
{
  auto const pool_info = initializers::CommandPoolCreateInfo(
    static_cast<u32>(init.device.get_queue_index(vkb::QueueType::graphics).value()));

  if (init.disp.createCommandPool(&pool_info, nullptr, &data.command_pool) != VK_SUCCESS) {
    std::cout << "failed to create command pool\n";
    return -1;
  }
  return 0;
}

auto create_command_buffers(Init &init, RenderData &data) -> int
{
  data.command_buffers.resize(data.swapchain_image_views.size());

  auto const alloc_info = initializers::CommandBufferAllocateInfo(
    data.command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, static_cast<u32>(data.command_buffers.size()));

  if (init.disp.allocateCommandBuffers(&alloc_info, data.command_buffers.data()) != VK_SUCCESS) {
    return -1;
  }

  for (size_t i = 0; i < data.command_buffers.size(); i++) {
    auto const begin_info = initializers::CommandBufferBeginInfo();

    if (init.disp.beginCommandBuffer(data.command_buffers.at(i), &begin_info) != VK_SUCCESS) {
      return -1;
    }

    record_triangle_draw(init, data, data.command_buffers.at(i), i);

    if (init.disp.endCommandBuffer(data.command_buffers.at(i)) != VK_SUCCESS) {
      std::cout << "failed to record command buffer\n";
      return -1;
    }
  }
  return 0;
}

auto sync_mesh_to_gpu(Init &init, RenderData &data) -> bool
{
  init.disp.deviceWaitIdle();

  if (!upload_mesh_buffers(init, data)) { return false; }
  if (!refresh_mesh_descriptor_heap(init, data)) { return false; }

  init.disp.destroyCommandPool(data.command_pool, nullptr);
  data.command_buffers.clear();

  if (0 != create_command_pool(init, data)) { return false; }
  return create_command_buffers(init, data) == 0;
}

auto create_sync_objects(Init &init, RenderData &data) -> int
{
  data.available_semaphores.resize(k_max_frames_in_flight);
  data.finished_semaphore.resize(init.swapchain.image_count);
  data.in_flight_fences.resize(k_max_frames_in_flight);
  data.image_in_flight.resize(init.swapchain.image_count, VK_NULL_HANDLE);

  auto const semaphore_info = initializers::SemaphoreCreateInfo();
  auto const fence_info = initializers::FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);

  for (size_t i = 0; i < init.swapchain.image_count; i++) {
    if (init.disp.createSemaphore(&semaphore_info, nullptr, &data.finished_semaphore.at(i)) != VK_SUCCESS) {
      std::cout << "failed to create sync objects\n";
      return -1;
    }
  }

  for (size_t i = 0; i < k_max_frames_in_flight; i++) {
    if (init.disp.createSemaphore(&semaphore_info, nullptr, &data.available_semaphores.at(i)) != VK_SUCCESS
        || init.disp.createFence(&fence_info, nullptr, &data.in_flight_fences.at(i)) != VK_SUCCESS) {
      std::cout << "failed to create sync objects\n";
      return -1;
    }
  }
  return 0;
}

auto draw_frame(Init &init, RenderData &data) -> int
{
  init.disp.waitForFences(1, &data.in_flight_fences.at(data.current_frame), VK_TRUE, UINT64_MAX);

  uint32_t image_index = 0;
  VkResult result = init.disp.acquireNextImageKHR(
    init.swapchain, UINT64_MAX, data.available_semaphores.at(data.current_frame), VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return recreate_swapchain(init, data);
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    std::cout << "failed to acquire swapchain image. Error " << result << "\n";
    return -1;
  }

  if (data.image_in_flight.at(image_index) != VK_NULL_HANDLE) {
    init.disp.waitForFences(1, &data.image_in_flight.at(image_index), VK_TRUE, UINT64_MAX);
  }
  data.image_in_flight.at(image_index) = data.in_flight_fences.at(data.current_frame);

  std::array<VkSemaphore, 1> wait_semaphores = { data.available_semaphores.at(data.current_frame) };
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  std::array<VkSemaphore, 1> signal_semaphores = { data.finished_semaphore.at(image_index) };

  auto const submit_info = initializers::SubmitInfo(
    wait_semaphores, wait_stages, std::span{ &data.command_buffers.at(image_index), 1 }, signal_semaphores);

  init.disp.resetFences(1, &data.in_flight_fences.at(data.current_frame));

  if (init.disp.queueSubmit(data.graphics_queue, 1, &submit_info, data.in_flight_fences.at(data.current_frame))
      != VK_SUCCESS) {
    std::cout << "failed to submit draw command buffer\n";
    return -1;
  }

  std::array<VkSwapchainKHR, 1> const swap_chains = { init.swapchain };
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
  for (size_t i = 0; i < init.swapchain.image_count; i++) {
    init.disp.destroySemaphore(data.finished_semaphore.at(i), nullptr);
  }
  for (size_t i = 0; i < k_max_frames_in_flight; i++) {
    init.disp.destroySemaphore(data.available_semaphores.at(i), nullptr);
    init.disp.destroyFence(data.in_flight_fences.at(i), nullptr);
  }

  init.disp.destroyCommandPool(data.command_pool, nullptr);

  destroy_mesh_buffers(init, data);
  destroy_triangle_sort(init, data);

  init.disp.destroyPipeline(data.graphics_pipeline, nullptr);

  init.swapchain.destroy_image_views(data.swapchain_image_views);

  vkb::destroy_swapchain(init.swapchain);
  vkb::destroy_device(init.device);
  vkb::destroy_surface(init.instance, init.surface);
  vkb::destroy_instance(init.instance);
  destroy_window_glfw(init.window);
}

}// namespace vkgsplat
