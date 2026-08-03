#include "gs/rasterization.hpp"

#include "app_state.hpp"
#include "backend/vulkan/initializers.hpp"
#include "backend/vulkan/sync_objects/barrier.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <print>

#include <glm/gtc/type_ptr.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

namespace {

  void DestroyColorTarget(Init const &init, RenderData &data)
  {
    if (data.color_image_view != VK_NULL_HANDLE) {
      init.disp.destroyImageView(data.color_image_view, nullptr);
      data.color_image_view = VK_NULL_HANDLE;
    }
    if (data.color_image != VK_NULL_HANDLE || data.color_allocation != VK_NULL_HANDLE) {
      vmaDestroyImage(init.gpu_allocator.vma_allocator(), data.color_image, data.color_allocation);
      data.color_image = VK_NULL_HANDLE;
      data.color_allocation = VK_NULL_HANDLE;
    }
    data.color_width = 0;
    data.color_height = 0;
  }

  [[nodiscard]] auto CreateColorTarget(Init &init, RenderData &data) -> bool
  {
    DestroyColorTarget(init, data);

    data.color_width = init.swapchain->extent().width;
    data.color_height = init.swapchain->extent().height;
    if (data.color_width == 0 || data.color_height == 0) {
      std::println("Rasterize requires a non-zero swapchain extent!");
      return false;
    }

    auto image_info = initializers::ImageCreateInfo();
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = data.color_format;
    image_info.extent = { .width = data.color_width, .height = data.color_height, .depth = 1 };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(init.gpu_allocator.vma_allocator(),
          &image_info,
          &alloc_info,
          &data.color_image,
          &data.color_allocation,
          nullptr)
        != VK_SUCCESS) {
      std::println("Failed to create raster color target!");
      DestroyColorTarget(init, data);
      return false;
    }

    VkImageSubresourceRange const subresource_range = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
    };
    auto const view_info =
      initializers::ImageViewCreateInfo(data.color_image, VK_IMAGE_VIEW_TYPE_2D, data.color_format, subresource_range);
    if (init.disp.createImageView(&view_info, nullptr, &data.color_image_view) != VK_SUCCESS) {
      std::println("Failed to create raster color target view!");
      DestroyColorTarget(init, data);
      return false;
    }

    return true;
  }

  void PushRasterConstants(Init const &init, RasterPushConstants const &push_constants, VkCommandBuffer command_buffer)
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

auto InitRasterization(Init &init, RenderData &data) -> bool { return CreateColorTarget(init, data); }

auto RecreateRasterizationColorTarget(Init &init, RenderData &data) -> bool { return CreateColorTarget(init, data); }

void DispatchRasterization(Init const &init,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index)
{
  Barrier::compute_to_graphics(init.disp, command_buffer);

  VkExtent2D const extent = { .width = data.color_width, .height = data.color_height };

  VkImageSubresourceRange const color_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  auto target_to_color = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, data.color_image, color_range);
  target_to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  auto swap_to_dst = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    init.swapchain->images().at(image_index),
    color_range);
  swap_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  std::array<VkImageMemoryBarrier, 2> prep_barriers = { target_to_color, swap_to_dst };
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    static_cast<uint32_t>(prep_barriers.size()),
    prep_barriers.data());

  std::array<float, 4> clear_rgba{};
  std::memcpy(clear_rgba.data(), glm::value_ptr(push_constants.background), 3U * sizeof(float));
  VkClearValue clear_value{};
  std::memcpy(&clear_value, clear_rgba.data(), sizeof(clear_rgba));

  VkRenderingAttachmentInfo const color_attachment = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = data.color_image_view,
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
  PushRasterConstants(init, push_constants, command_buffer);
  init.disp.cmdDrawIndirect(command_buffer, data.draw_indirect_buffer.handle, 0, 1, sizeof(VkDrawIndirectCommand));

  init.disp.cmdEndRendering(command_buffer);

  auto target_to_src = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.color_image, color_range);
  target_to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  target_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &target_to_src);

  VkImageBlit const blit = {
    .srcSubresource =
      {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    .srcOffsets =
      {
        { .x = 0, .y = 0, .z = 0 },
        { .x = static_cast<int32_t>(data.color_width), .y = static_cast<int32_t>(data.color_height), .z = 1 },
      },
    .dstSubresource =
      {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    .dstOffsets =
      {
        { .x = 0, .y = 0, .z = 0 },
        { .x = static_cast<int32_t>(init.swapchain->extent().width),
          .y = static_cast<int32_t>(init.swapchain->extent().height),
          .z = 1 },
      },
  };
  init.disp.cmdBlitImage(command_buffer,
    data.color_image,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    init.swapchain->images().at(image_index),
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1,
    &blit,
    VK_FILTER_NEAREST);

  // Leave swapchain in TRANSFER_DST_OPTIMAL for ImGui / present.
}

void DestroyRasterization(Init &init, RenderData &data)
{
  data.rasterize_algorithm.destroy(init);
  DestroyColorTarget(init, data);
}

}// namespace vkgsplat::gs
