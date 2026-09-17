#include "gs/rasterization.hpp"

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vkexec/barrier.hpp>

#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <print>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <vkexec/image.hpp>
#include <vkexec/image_view.hpp>
#include <vkexec/sync_wait.hpp>
#include <vkexec_extensions/descriptor_heap/push_data.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

namespace {

  void DestroyColorTarget(vulkan::Context const & /*context*/, RenderData &data)
  {
    // View must be destroyed before the image it references.
    data.color_image_view.reset();
    data.color_image.reset();
    data.color_width = 0;
    data.color_height = 0;
    data.quilt_tile_extent = {};
  }

  [[nodiscard]] auto CreateColorTarget(vulkan::Context &context, RenderData &data) -> bool
  {
    DestroyColorTarget(context, data);

    if (context.vkexec_context == nullptr) {
      std::println("vkexec context missing for raster color target");
      return false;
    }

    u32 const tile_w = context.swapchain->vk_extent().width;
    u32 const tile_h = context.swapchain->vk_extent().height;
    if (tile_w == 0 || tile_h == 0) {
      std::println("Rasterize requires a non-zero swapchain extent!");
      return false;
    }

    u32 const cols = data.lfd_emulate_active ? 1U : std::max(1U, data.lfd_grid.at(0));
    u32 const rows = data.lfd_emulate_active ? 1U : std::max(1U, data.lfd_grid.at(1));
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

    auto created_image = vkexec::try_sync_wait_value(vkexec::image::create(*context.vkexec_context,
      vkexec::image_create_info{
        .width = data.color_width,
        .height = data.color_height,
        .usage = vkexec::image_usage::color_storage,
        .format = data.color_format,
      }));
    if (!created_image) {
      std::println("Failed to create raster color target: {}", created_image.error().message());
      DestroyColorTarget(context, data);
      return false;
    }
    data.color_image = std::move(*created_image);

    auto created_view =
      vkexec::try_sync_wait_value(vkexec::image_view::create(*context.vkexec_context, *data.color_image));
    if (!created_view) {
      std::println("Failed to create raster color target view: {}", created_view.error().message());
      DestroyColorTarget(context, data);
      return false;
    }
    data.color_image_view = std::move(*created_view);
    data.color_format = data.color_image->format();

    return true;
  }

  void PushRasterConstants(vulkan::Context const &context, RasterPushConstants const &push_constants, VkCommandBuffer command_buffer)
  {
    (void)vkexec::cmd_push_data(*context.vkexec_context, command_buffer, push_constants);
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
    if (!data.color_image_view.has_value() || !data.draw_indirect_buffer) { return; }

    std::array<float, 4> clear_rgba{};
    std::memcpy(clear_rgba.data(), glm::value_ptr(push_constants.background), 3U * sizeof(float));
    VkClearValue clear_value{};
    std::memcpy(&clear_value, clear_rgba.data(), sizeof(clear_rgba));

    VkRenderingAttachmentInfo const color_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .pNext = nullptr,
      .imageView = data.color_image_view->handle(),
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
    context.disp.cmdDrawIndirect(
      command_buffer, data.draw_indirect_buffer->handle(), 0, 1, sizeof(VkDrawIndirectCommand));

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
  if (!data.color_image.has_value()) { return; }

  vkexec::image_barrier(command_buffer,
    {
      .image = data.color_image->handle(),
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .src_access = 0,
      .dst_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    });
  vkexec::image_barrier(command_buffer,
    {
      .image = context.swapchain->images().at(image_index),
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .src_access = 0,
      .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
    });
}

void DrawQuiltTile(vulkan::Context const &context,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  VkRect2D tile,
  bool clear_attachment)
{
  vkexec::barrier::compute_to_graphics(command_buffer);

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
  if (!data.color_image.has_value()) { return; }

  vkexec::image_barrier(command_buffer,
    {
      .image = data.color_image->handle(),
      .old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dst_access = VK_ACCESS_TRANSFER_READ_BIT,
    });

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
    data.color_image->handle(),
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
  (void)context;
  data.rasterize_algorithm.reset();
  DestroyColorTarget(context, data);
}

}// namespace vkgsplat::gs
