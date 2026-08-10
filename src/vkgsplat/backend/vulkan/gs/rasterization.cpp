#include "gs/rasterization.hpp"

#include "app_state.hpp"
#include "backend/vulkan/initializers.hpp"
#include "backend/vulkan/sync_objects/barrier.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <algorithm>
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

  void DestroyColorTarget(vulkan::Context const &context, RenderData &data)
  {
    if (data.color_image_view != VK_NULL_HANDLE) {
      context.disp.destroyImageView(data.color_image_view, nullptr);
      data.color_image_view = VK_NULL_HANDLE;
    }
    if (data.color_image != VK_NULL_HANDLE || data.color_allocation != VK_NULL_HANDLE) {
      vmaDestroyImage(context.gpu_allocator.vma_allocator(), data.color_image, data.color_allocation);
      data.color_image = VK_NULL_HANDLE;
      data.color_allocation = VK_NULL_HANDLE;
    }
    data.color_width = 0;
    data.color_height = 0;
    data.quilt_tile_extent = {};
  }

  [[nodiscard]] auto CreateColorTarget(vulkan::Context &context, RenderData &data) -> bool
  {
    DestroyColorTarget(context, data);

    u32 const tile_w = context.swapchain->vk_extent().width;
    u32 const tile_h = context.swapchain->vk_extent().height;
    if (tile_w == 0 || tile_h == 0) {
      std::println("Rasterize requires a non-zero swapchain extent!");
      return false;
    }

    u32 const cols = std::max(1U, data.lfd_grid.at(0));
    u32 const rows = std::max(1U, data.lfd_grid.at(1));
    u64 const atlas_w = static_cast<u64>(tile_w) * static_cast<u64>(cols);
    u64 const atlas_h = static_cast<u64>(tile_h) * static_cast<u64>(rows);
    u32 const max_dim = context.device.physical_device.properties.limits.maxImageDimension2D;
    if (atlas_w == 0 || atlas_h == 0 || atlas_w > max_dim || atlas_h > max_dim) {
      std::println("LFD atlas {}x{} exceeds device maxImageDimension2D ({})!", atlas_w, atlas_h, max_dim);
      return false;
    }

    data.quilt_tile_extent = { .width = tile_w, .height = tile_h };
    data.color_width = static_cast<u32>(atlas_w);
    data.color_height = static_cast<u32>(atlas_h);

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
    if (vmaCreateImage(context.gpu_allocator.vma_allocator(),
          &image_info,
          &alloc_info,
          &data.color_image,
          &data.color_allocation,
          nullptr)
        != VK_SUCCESS) {
      std::println("Failed to create raster color target!");
      DestroyColorTarget(context, data);
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
    if (context.disp.createImageView(&view_info, nullptr, &data.color_image_view) != VK_SUCCESS) {
      std::println("Failed to create raster color target view!");
      DestroyColorTarget(context, data);
      return false;
    }

    return true;
  }

  void PushRasterConstants(vulkan::Context const &context, RasterPushConstants const &push_constants, VkCommandBuffer command_buffer)
  {
    VkPushDataInfoEXT const push_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
      .pNext = nullptr,
      .offset = 0,
      .data = { .address = &push_constants, .size = sizeof(RasterPushConstants) },
    };
    context.cmd_push_data(command_buffer, &push_info);
  }

  [[nodiscard]] auto ColorRange() -> VkImageSubresourceRange
  {
    return {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
    };
  }

  void DrawIntoColorTarget(vulkan::Context const &context,
    RenderData const &data,
    RasterPushConstants const &push_constants,
    VkCommandBuffer command_buffer,
    VkRect2D const render_area,
    VkViewport const viewport,
    VkRect2D const scissor,
    bool clear_attachment)
  {
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
      .loadOp = clear_attachment ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clear_value,
    };

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

    context.disp.cmdBeginRendering(command_buffer, &rendering_info);
    context.disp.cmdSetViewport(command_buffer, 0, 1, &viewport);
    context.disp.cmdSetScissor(command_buffer, 0, 1, &scissor);

    context.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);
    PushRasterConstants(context, push_constants, command_buffer);
    context.disp.cmdDrawIndirect(command_buffer, data.draw_indirect_buffer.handle, 0, 1, sizeof(VkDrawIndirectCommand));

    context.disp.cmdEndRendering(command_buffer);
  }

}// namespace

auto InitRasterization(vulkan::Context &context, RenderData &data) -> bool { return CreateColorTarget(context, data); }

auto RecreateRasterizationColorTarget(vulkan::Context &context, RenderData &data) -> bool { return CreateColorTarget(context, data); }

void PrepareQuiltPresent(vulkan::Context const &context,
  RenderData const &data,
  VkCommandBuffer command_buffer,
  size_t image_index)
{
  VkImageSubresourceRange const color_range = ColorRange();

  auto target_to_color = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, data.color_image, color_range);
  target_to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  auto swap_to_dst = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    context.swapchain->images().at(image_index),
    color_range);
  swap_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  std::array<VkImageMemoryBarrier, 2> prep_barriers = { target_to_color, swap_to_dst };
  context.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    static_cast<uint32_t>(prep_barriers.size()),
    prep_barriers.data());
}

void DrawQuiltTile(vulkan::Context const &context,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  VkRect2D tile,
  bool clear_attachment)
{
  Barrier::compute_to_graphics(context.disp, command_buffer);

  // First tile may clear the full atlas; later tiles LOAD and only write their viewport.
  VkRect2D const render_area = clear_attachment
                                 ? VkRect2D{ .offset = { .x = 0, .y = 0 },
                                     .extent = { .width = data.color_width, .height = data.color_height } }
                                 : tile;

  VkViewport const viewport = {
    .x = static_cast<float>(tile.offset.x),
    .y = static_cast<float>(tile.offset.y),
    .width = static_cast<float>(tile.extent.width),
    .height = static_cast<float>(tile.extent.height),
    .minDepth = 0.0F,
    .maxDepth = 1.0F,
  };

  DrawIntoColorTarget(context, data, push_constants, command_buffer, render_area, viewport, tile, clear_attachment);
}

void BlitQuiltToSwapchain(vulkan::Context const &context,
  RenderData const &data,
  VkCommandBuffer command_buffer,
  size_t image_index)
{
  VkImageSubresourceRange const color_range = ColorRange();

  auto target_to_src = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.color_image, color_range);
  target_to_src.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  target_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  context.disp.cmdPipelineBarrier(command_buffer,
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
        { .x = static_cast<int32_t>(context.swapchain->vk_extent().width),
          .y = static_cast<int32_t>(context.swapchain->vk_extent().height),
          .z = 1 },
      },
  };
  context.disp.cmdBlitImage(command_buffer,
    data.color_image,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    context.swapchain->images().at(image_index),
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1,
    &blit,
    VK_FILTER_NEAREST);

  // Leave swapchain in TRANSFER_DST_OPTIMAL for ImGui / present.
}

void DispatchRasterization(vulkan::Context const &context,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index)
{
  PrepareQuiltPresent(context, data, command_buffer, image_index);

  VkExtent2D const extent = { .width = data.color_width, .height = data.color_height };
  VkRect2D const full_tile = {
    .offset = { .x = 0, .y = 0 },
    .extent = extent,
  };
  DrawQuiltTile(context, data, push_constants, command_buffer, full_tile, true);

  BlitQuiltToSwapchain(context, data, command_buffer, image_index);
}

void DestroyRasterization(vulkan::Context &context, RenderData &data)
{
  data.rasterize_algorithm.destroy(context);
  DestroyColorTarget(context, data);
}

}// namespace vkgsplat::gs
