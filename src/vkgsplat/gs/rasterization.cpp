#include "gs/rasterization.hpp"

#include "app_state.hpp"
#include "backend/vulkan/initializers.hpp"
#include "backend/vulkan/sync_objects/barrier.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <array>
#include <cstddef>
#include <cstring>

#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

namespace {

  void
    push_raster_constants(Init const &init, RasterPushConstants const &push_constants, VkCommandBuffer command_buffer)
  {
    VkPushDataInfoEXT const push_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
      .pNext = nullptr,
      .offset = 0,
      .data = { .address = &push_constants, .size = sizeof(RasterPushConstants) },
    };
    init.cmd_push_data(command_buffer, &push_info);
  }

}// namespace

auto init_rasterization(Init & /*init*/, RenderData &data) -> bool
{
  // HW quad path draws directly to the swapchain; no compute color target.
  data.color_width = 0;
  data.color_height = 0;
  return true;
}

auto recreate_rasterization_color_target(Init &init, RenderData &data) -> bool
{
  data.color_width = init.swapchain->extent().width;
  data.color_height = init.swapchain->extent().height;
  return true;
}

void dispatch_rasterization(Init const &init,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index)
{
  // Sorted instance IDs + draw indirect args must be visible to VS / DrawIndirect.
  Barrier::compute_to_graphics(init.disp, command_buffer);

  VkImage swapchain_image = init.swapchain->images().at(image_index);
  VkImageView swapchain_view = init.swapchain->image_views().at(image_index);
  VkExtent2D const extent = init.swapchain->extent();

  VkImageSubresourceRange const color_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  auto to_color = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, swapchain_image, color_range);
  to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &to_color);

  std::array<float, 4> clear_rgba{};
  std::memcpy(clear_rgba.data(), glm::value_ptr(push_constants.background), 3U * sizeof(float));
  VkClearValue clear_value{};
  std::memcpy(&clear_value, clear_rgba.data(), sizeof(clear_rgba));

  VkRenderingAttachmentInfo const color_attachment = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = swapchain_view,
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .resolveMode = VK_RESOLVE_MODE_NONE,
    .resolveImageView = VK_NULL_HANDLE,
    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = clear_value,
  };

  VkRenderingInfo const rendering_info = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .pNext = nullptr,
    .flags = 0,
    .renderArea = { .offset = { .x = 0, .y = 0 }, .extent = extent },
    .layerCount = 1,
    .viewMask = 0,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment,
    .pDepthAttachment = nullptr,
    .pStencilAttachment = nullptr,
  };

  init.disp.cmdBeginRendering(command_buffer, &rendering_info);

  VkViewport const viewport = {
    .x = 0.0F,
    .y = 0.0F,
    .width = static_cast<float>(extent.width),
    .height = static_cast<float>(extent.height),
    .minDepth = 0.0F,
    .maxDepth = 1.0F,
  };
  VkRect2D const scissor = {
    .offset = { .x = 0, .y = 0 },
    .extent = extent,
  };
  init.disp.cmdSetViewport(command_buffer, 0, 1, &viewport);
  init.disp.cmdSetScissor(command_buffer, 0, 1, &scissor);

  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);
  push_raster_constants(init, push_constants, command_buffer);
  init.disp.cmdDrawIndirect(command_buffer, data.draw_indirect_buffer.handle, 0, 1, sizeof(VkDrawIndirectCommand));

  init.disp.cmdEndRendering(command_buffer);

  // Leave swapchain in COLOR_ATTACHMENT_OPTIMAL for ImGui overlay / present transition.
}

void destroy_rasterization(Init &init, RenderData &data)
{
  data.rasterize_algorithm.destroy(init);
  data.color_image = VK_NULL_HANDLE;
  data.color_allocation = VK_NULL_HANDLE;
  data.color_width = 0;
  data.color_height = 0;
}

}// namespace vkgsplat::gs
