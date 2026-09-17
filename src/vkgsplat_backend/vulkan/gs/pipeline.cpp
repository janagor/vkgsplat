#include "gs/pipeline.hpp"

#include "app_state.hpp"
#include "vulkan/frame_context.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include <vkexec/barrier.hpp>
#include "compute/op_fill_buffer.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/operations.hpp"
#include "gs/rasterization.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/lfd_config.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/ext/vector_float4.hpp>
#include <glm/trigonometric.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

namespace {

  [[nodiscard]] auto IsMonoQuilt(RenderData const &data) -> bool
  { return data.lfd_grid.at(0) <= 1U && data.lfd_grid.at(1) <= 1U; }

  [[nodiscard]] auto PhaseACullMargin(RenderData const &data) -> f32
  {
    if (IsMonoQuilt(data)) { return kDefaultProjectionCullMargin; }
    // Keep fringe Gaussians visible to extreme off-axis tiles when sorting once.
    constexpr f32 kCullMarginSlope = 1.5F;
    constexpr f32 kHalfConeFactor = 0.5F;
    f32 const half_cone = static_cast<f32>(glm::radians(data.view_cone_deg * static_cast<f64>(kHalfConeFactor)));
    return std::max(kDefaultProjectionCullMargin, 1.0F + (kCullMarginSlope * std::tan(half_cone)));
  }

  void RecordPhaseA(RenderData &data)
  {
    data.gs_sequence.emplace<OpProjection>()
      .emplace<compute::OpFillBuffer>(compute::FillBufferParams{
        .buffer = data.instance_count_buffer.handle(),
        .offset = 0,
        .size = sizeof(u32),
        .value = 0U,
      })
      .emplace<OpBinning>()
      .emplace<OpPrepareSort>()
      .emplace<OpRadixSort>();
  }

  struct QuiltTileRect
  {
    u32 origin_x{};
    u32 origin_y{};
    u32 width{};
    u32 height{};
  };

  void ApplyQuiltTilePush(RenderData &data, QuiltView const &view, QuiltTileRect const &tile)
  {
    data.project_push.view = view.view;
    data.project_push.projection = view.projection;
    data.project_push.viewport = { static_cast<f32>(tile.width), static_cast<f32>(tile.height) };
    data.project_push.cull_margin = kDefaultProjectionCullMargin;
    data.project_push.camera_position = glm::vec4{ view.position, 0.0F };

    data.raster_push.camera_position = glm::vec4{ view.position, 0.0F };
    data.raster_push.viewport = { tile.width, tile.height };
    data.raster_push.tiles_x = (tile.width + kTileSize - 1U) / kTileSize;
    data.raster_push.tile_offset = { tile.origin_x, tile.origin_y };
  }

  class ScopedGpuPass
  {
  public:
    ScopedGpuPass(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer, GpuPass pass)
      : context_(context), data_(data), command_buffer_(command_buffer), pass_(pass), active_(data.gpu_pass_timer.enabled())
    {
      if (active_) { data_.gpu_pass_timer.write(context_, data_.current_slot, pass_, false, command_buffer_); }
    }

    ScopedGpuPass(ScopedGpuPass const &) = delete;
    auto operator=(ScopedGpuPass const &) -> ScopedGpuPass & = delete;
    ScopedGpuPass(ScopedGpuPass &&) = delete;
    auto operator=(ScopedGpuPass &&) -> ScopedGpuPass & = delete;

    ~ScopedGpuPass()
    {
      if (active_) { data_.gpu_pass_timer.write(context_, data_.current_slot, pass_, true, command_buffer_); }
    }

  private:
    vulkan::Context const &context_;
    RenderData const &data_;
    VkCommandBuffer command_buffer_;
    GpuPass pass_;
    bool active_;
  };

}// namespace

void RecordGsPipeline(RenderData &data)
{
  // Phase A only. Rasterization is recorded in EvalGsPipeline (mono or quilt Phase B).
  RecordPhaseA(data);
}

void EvalGsPipeline(vulkan::Context &context, RenderData &data, VkCommandBuffer command_buffer)
{
  size_t const slot = data.current_slot;
  if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.begin_frame(context, slot, command_buffer); }

  data.project_push.cull_margin = PhaseACullMargin(data);
  data.gs_sequence.eval(context, data, command_buffer);

  if (IsMonoQuilt(data)) {
    OpRasterization{}.record(context, data, command_buffer);
  } else if (data.lfd_emulate_active) {
    FrameSetup const &setup = FrameSetupFor(data, slot);

    // Emulate mode renders a single full-screen view (no atlas offsets).
    u32 const tile_w = context.swapchain->vk_extent().width;
    u32 const tile_h = context.swapchain->vk_extent().height;
    f64 const tile_aspect = static_cast<f64>(tile_w) / static_cast<f64>(std::max(1U, tile_h));

    LfdGridCell const logical = LfdLogicalCellForGridPosition(data.lfd_view_order,
      data.lfd_grid,
      data.lfd_emulate_cell.at(0),
      data.lfd_emulate_cell.at(1));

    QuiltView const view = MakeQuiltView(QuiltViewRequest{
      .center = &setup.camera,
      .grid = data.lfd_grid,
      .col = logical.col,
      .row = logical.row,
      .view_cone_deg = data.view_cone_deg,
      .focal_distance = data.lfd_focal_distance,
      .tile_aspect = tile_aspect,
    });

    QuiltTileRect const tile_rect{
      .origin_x = 0U,
      .origin_y = 0U,
      .width = tile_w,
      .height = tile_h,
    };

    // Match the quilt per-cell push constants so viewport / tiles_x / tile_offset are consistent.
    ApplyQuiltTilePush(data, view, tile_rect);

    // Phase A (projection+projection timestamps) already ran once for the center view.
    // Re-run projection for the emulated off-axis camera so rasterization sees updated projected data.
    RecordProjection(context, data, command_buffer, false);

    OpRasterization{}.record(context, data, command_buffer);
  } else {
    // One begin/end pair for all quilt cells: re-project + draw + blit (raster bucket).
    ScopedGpuPass const raster_timer{ context, data, command_buffer, GpuPass::kRasterize };

    FrameSetup const &setup = FrameSetupFor(data, slot);
    u32 const cols = std::max(1U, data.lfd_grid.at(0));
    u32 const rows = std::max(1U, data.lfd_grid.at(1));
    u32 const tile_w =
      data.quilt_tile_extent.width != 0U ? data.quilt_tile_extent.width : context.swapchain->vk_extent().width;
    u32 const tile_h =
      data.quilt_tile_extent.height != 0U ? data.quilt_tile_extent.height : context.swapchain->vk_extent().height;
    f64 const tile_aspect = static_cast<f64>(tile_w) / static_cast<f64>(std::max(1U, tile_h));

    PrepareQuiltPresent(context, data, command_buffer, data.present_image_index);

    bool first_tile = true;
    for (u32 row = 0U; row < rows; ++row) {
      for (u32 col = 0U; col < cols; ++col) {
        if (!first_tile) { vkexec::barrier::graphics_to_compute(command_buffer); }

        LfdGridCell const logical = LfdLogicalCellForGridPosition(data.lfd_view_order, data.lfd_grid, col, row);

        QuiltView const view = MakeQuiltView(QuiltViewRequest{
          .center = &setup.camera,
          .grid = data.lfd_grid,
          .col = logical.col,
          .row = logical.row,
          .view_cone_deg = data.view_cone_deg,
          .focal_distance = data.lfd_focal_distance,
          .tile_aspect = tile_aspect,
        });
        u32 const origin_x = col * tile_w;
        u32 const origin_y = row * tile_h;
        QuiltTileRect const tile_rect{
          .origin_x = origin_x,
          .origin_y = origin_y,
          .width = tile_w,
          .height = tile_h,
        };
        ApplyQuiltTilePush(data, view, tile_rect);

        // Untimed: Phase A already filled GpuPass::kProjection queries.
        RecordProjection(context, data, command_buffer, false);
        VkRect2D const draw_tile = {
          .offset = { .x = static_cast<int32_t>(origin_x), .y = static_cast<int32_t>(origin_y) },
          .extent = { .width = tile_w, .height = tile_h },
        };
        DrawQuiltTile(context, data, data.raster_push, command_buffer, draw_tile, first_tile);
        first_tile = false;
      }
    }

    BlitQuiltToSwapchain(context, data, command_buffer, data.present_image_index);
  }

  if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.mark_submitted(slot); }
}

void DestroyGsPipeline(RenderData &data) { data.gs_sequence.clear(); }

}// namespace vkgsplat::gs
