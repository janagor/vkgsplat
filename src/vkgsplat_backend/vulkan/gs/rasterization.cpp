#include "gs/rasterization.hpp"

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vkexec/barrier.hpp>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <print>
#include <system_error>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <vkexec/image_view.hpp>
#include <vkexec/sync_wait.hpp>
#include <vkexec_extensions/descriptor_heap/push_data.hpp>
#include <vkexec_vma/image.hpp>
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

    if (context.vkexec_context == nullptr || context.allocator == nullptr) {
      std::println("vkexec context or VMA allocator missing for raster color target");
      return false;
    }

    u32 const cols = data.lfd_emulate_active ? 1U : std::max(1U, data.lfd_grid.at(0));
    u32 const rows = data.lfd_emulate_active ? 1U : std::max(1U, data.lfd_grid.at(1));
    bool const use_quilt_size = !data.lfd_emulate_active && data.quilt_size.at(0) != 0U;
    u32 const tile_w = use_quilt_size ? data.quilt_size.at(0) / cols : data.output_extent.width;
    u32 const tile_h = use_quilt_size ? data.quilt_size.at(1) / rows : data.output_extent.height;
    if (tile_w == 0 || tile_h == 0) {
      std::println("Rasterize requires a non-zero quilt tile extent!");
      return false;
    }

    u64 const atlas_w = static_cast<u64>(tile_w) * static_cast<u64>(cols);
    u64 const atlas_h = static_cast<u64>(tile_h) * static_cast<u64>(rows);
    u32 const max_dim = context.device.physical_device.properties.limits.maxImageDimension2D;
    if (atlas_w == 0 || atlas_h == 0 || atlas_w > max_dim || atlas_h > max_dim) {
      std::println("LFD atlas {}x{} exceeds device maxImageDimension2D ({})!", atlas_w, atlas_h, max_dim);
      return false;
    }
    constexpr u64 kRgbaChannels = 4U;
    if (atlas_w > std::numeric_limits<size_t>::max() / kRgbaChannels / atlas_h) {
      std::println("LFD atlas byte size exceeds addressable memory!");
      return false;
    }

    data.quilt_tile_extent = { .width = tile_w, .height = tile_h };
    data.color_width = static_cast<u32>(atlas_w);
    data.color_height = static_cast<u32>(atlas_h);

    auto created_image = vkexec::try_sync_wait_value(vkexec::vma::factory::make_image(*context.allocator,
      vkexec::vma::image_create_info{
        .width = data.color_width,
        .height = data.color_height,
        .usage = vkexec::vma::image_usage::color_storage,
        .format = data.color_format,
      }));
    if (!created_image) {
      std::println("Failed to create raster color target: {}", created_image.error().message());
      DestroyColorTarget(context, data);
      return false;
    }
    data.color_image = std::move(*created_image);

    auto created_view =
      vkexec::try_sync_wait_value(vkexec::vma::factory::make_image_view(*context.vkexec_context, *data.color_image));
    if (!created_view) {
      std::println("Failed to create raster color target view: {}", created_view.error().message());
      DestroyColorTarget(context, data);
      return false;
    }
    data.color_image_view = std::move(*created_view);
    data.color_format = data.color_image->format();

    return true;
  }

  void PushRasterConstants(vulkan::Context const &context,
    RasterPushConstants const &push_constants,
    VkCommandBuffer command_buffer)
  { (void)vkexec::cmd_push_data(*context.vkexec_context, command_buffer, push_constants); }

  void DrawIntoColorTarget(vulkan::Context const &context,
    RenderData const &data,
    RasterPushConstants const &push_constants,
    VkCommandBuffer command_buffer,
    VkRect2D const render_area,
    VkViewport const viewport,
    VkRect2D const scissor,
    bool clear_attachment)
  {
    if (!data.color_image_view.has_value() || !data.draw_indirect_buffer || !data.graphics_pipeline.has_value()) {
      return;
    }

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

    context.disp.cmdBindPipeline(
      command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline->resources().pipeline);
    PushRasterConstants(context, push_constants, command_buffer);
    context.disp.cmdDrawIndirect(
      command_buffer, data.draw_indirect_buffer->handle(), 0, 1, sizeof(VkDrawIndirectCommand));

    context.disp.cmdEndRendering(command_buffer);
  }

}// namespace

auto InitRasterization(vulkan::Context &context, RenderData &data) -> bool { return CreateColorTarget(context, data); }

auto RecreateRasterizationColorTarget(vulkan::Context &context, RenderData &data) -> bool
{ return CreateColorTarget(context, data); }

auto BeginColorTargetFrame(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
  -> std::expected<void, Error>
{
  if (!data.color_image.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "raster color image is missing") };
  }

  auto color_barrier = vkexec::image_barrier(*context.vkexec_context,
    command_buffer,
    {
      .image = data.color_image->handle(),
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .src_access = 0,
      .dst_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    });
  if (!color_barrier) { return std::unexpected{ MakeError(std::errc::io_error, color_barrier.error().message()) }; }
  return {};
}

auto DrawQuiltTile(vulkan::Context const &context,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  VkRect2D tile,
  bool clear_attachment,
  bool synchronize_projection) -> std::expected<void, Error>
{
  if (synchronize_projection) {
    auto barrier = vkexec::barrier::compute_to_graphics(*context.vkexec_context, command_buffer);
    if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }
  }
  if (!clear_attachment) {
    VkMemoryBarrier const attachment_barrier{
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .pNext = nullptr,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    context.disp.cmdPipelineBarrier(command_buffer,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_DEPENDENCY_BY_REGION_BIT,
      1U,
      &attachment_barrier,
      0U,
      nullptr,
      0U,
      nullptr);
  }

  // First tile may clear the full atlas; later tiles LOAD and only write their viewport.
  VkRect2D const render_area = clear_attachment ? VkRect2D{ .offset = { .x = 0, .y = 0 },
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
  return {};
}

auto FinishColorTargetFrame(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
  -> std::expected<void, Error>
{
  if (!data.color_image.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "raster color image is missing") };
  }

  auto barrier = vkexec::image_barrier(*context.vkexec_context,
    command_buffer,
    {
      .image = data.color_image->handle(),
      .old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dst_access = VK_ACCESS_TRANSFER_READ_BIT,
    });
  if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }
  return {};
}

auto BlitQuiltToSwapchain(vulkan::Context const &context,
  RenderData const &data,
  VkCommandBuffer command_buffer,
  size_t image_index) -> std::expected<void, Error>
{
  if (context.swapchain == nullptr) { return {}; }
  if (!data.color_image.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "color target is not initialized") };
  }
  auto swapchain_barrier = vkexec::image_barrier(*context.vkexec_context,
    command_buffer,
    {
      .image = context.swapchain->images().at(image_index),
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .src_access = 0,
      .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
    });
  if (!swapchain_barrier) {
    return std::unexpected{ MakeError(std::errc::io_error, swapchain_barrier.error().message()) };
  }

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
  return {};
}

auto DispatchRasterization(vulkan::Context const &context,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index) -> std::expected<void, Error>
{
  if (auto recorded = BeginColorTargetFrame(context, data, command_buffer); !recorded) { return recorded; }

  VkExtent2D const extent = { .width = data.color_width, .height = data.color_height };
  VkRect2D const full_tile = {
    .offset = { .x = 0, .y = 0 },
    .extent = extent,
  };
  if (auto recorded = DrawQuiltTile(context, data, push_constants, command_buffer, full_tile, true); !recorded) {
    return recorded;
  }

  if (auto finished = FinishColorTargetFrame(context, data, command_buffer); !finished) { return finished; }
  return BlitQuiltToSwapchain(context, data, command_buffer, image_index);
}

void DestroyRasterization(vulkan::Context &context, RenderData &data)
{
  (void)context;
  data.rasterize_algorithm.reset();
  DestroyColorTarget(context, data);
}

}// namespace vkgsplat::gs
