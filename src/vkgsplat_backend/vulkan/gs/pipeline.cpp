#include "gs/pipeline.hpp"

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/operations.hpp"
#include "gs/projection.hpp"
#include "gs/rasterization.hpp"
#include "vulkan/frame_context.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "vulkan_context.hpp"
#include <vkexec/barrier.hpp>

#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/lfd_config.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <span>
#include <system_error>

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

  [[nodiscard]] auto RecordEmulatedView(vulkan::Context const &context,
    RenderData &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>
  {
    FrameSetup const &setup = FrameSetupFor(data, data.current_slot);
    u32 const tile_w = data.output_extent.width;
    u32 const tile_h = data.output_extent.height;
    f64 const tile_aspect = static_cast<f64>(tile_w) / static_cast<f64>(std::max(1U, tile_h));
    LfdGridCell const logical = LfdLogicalCellForGridPosition(
      data.lfd_view_order, data.lfd_grid, data.lfd_emulate_cell.at(0), data.lfd_emulate_cell.at(1));
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
    ApplyQuiltTilePush(data, view, tile_rect);

    // Phase A projected the center view; rasterization needs this off-axis view.
    data.gpu_pass_timer.write_view(
      context, data.current_slot, 0U, GpuPass::kMultiviewProjection, false, command_buffer);
    if (auto recorded = RecordProjection(context, data, command_buffer, false); !recorded) { return recorded; }
    data.gpu_pass_timer.write_view(context, data.current_slot, 0U, GpuPass::kMultiviewProjection, true, command_buffer);
    return RecordRasterization(context, data, command_buffer);
  }

  [[nodiscard]] auto RecordQuiltCell(vulkan::Context &context,
    RenderData &data,
    VkCommandBuffer command_buffer,
    FrameSetup const &setup,
    QuiltTileRect const &tile,
    f64 tile_aspect,// NOLINT(bugprone-easily-swappable-parameters)
    u32 col,
    u32 row,
    bool first_tile,
    bool project_view,
    bool synchronize_projection,
    u32 projected_view_index) -> std::expected<void, Error>
  {
    if (project_view && !first_tile) {
      auto barrier = vkexec::barrier::graphics_to_compute(*context.vkexec_context, command_buffer);
      if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }
    }

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
    ApplyQuiltTilePush(data, view, tile);
    data.raster_push.projected_view_index = projected_view_index;
    u32 const view_index = (row * data.lfd_grid.at(0)) + col;

    if (project_view) {
      data.gpu_pass_timer.write_view(
        context, data.current_slot, view_index, GpuPass::kMultiviewProjection, false, command_buffer);
      if (auto recorded = RecordProjection(context, data, command_buffer, false); !recorded) { return recorded; }
      data.gpu_pass_timer.write_view(
        context, data.current_slot, view_index, GpuPass::kMultiviewProjection, true, command_buffer);
    }
    VkRect2D const draw_tile = {
      .offset = { .x = static_cast<int32_t>(tile.origin_x), .y = static_cast<int32_t>(tile.origin_y) },
      .extent = { .width = tile.width, .height = tile.height },
    };
    data.gpu_pass_timer.write_view(context, data.current_slot, view_index, GpuPass::kRasterize, false, command_buffer);
    auto recorded =
      DrawQuiltTile(context, data, data.raster_push, command_buffer, draw_tile, first_tile, synchronize_projection);
    if (!recorded) { return recorded; }
    data.gpu_pass_timer.write_view(context, data.current_slot, view_index, GpuPass::kRasterize, true, command_buffer);
    return {};
  }

  struct QuiltBatch
  {
    u32 cols{};
    u32 tile_width{};
    u32 tile_height{};
    u32 begin{};
    u32 count{};
    u32 size{};
    f64 tile_aspect{};
  };

  [[nodiscard]] auto QuiltTileForView(QuiltBatch const &batch, u32 view_index) -> QuiltTileRect
  {
    u32 const col = view_index % batch.cols;
    u32 const row = view_index / batch.cols;
    return QuiltTileRect{ .origin_x = col * batch.tile_width,
      .origin_y = row * batch.tile_height,
      .width = batch.tile_width,
      .height = batch.tile_height };
  }

  [[nodiscard]] auto RecordQuiltBatchProjection(vulkan::Context &context,
    RenderData &data,
    VkCommandBuffer command_buffer,
    FrameSetup const &setup,
    QuiltBatch const &batch) -> std::expected<void, Error>
  {
    if (batch.size == 1U) { return {}; }
    if (batch.begin != 0U) {
      auto barrier = vkexec::barrier::graphics_to_compute(*context.vkexec_context, command_buffer);
      if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }
    }
    if (auto barrier = vkexec::barrier::compute_to_compute(*context.vkexec_context, command_buffer); !barrier) {
      return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) };
    }

    std::array<ProjectPushConstants, kMaxProjectionBatchSize> cameras{};
    for (u32 local = 0U; local < batch.count; ++local) {
      u32 const view_index = batch.begin + local;
      u32 const col = view_index % batch.cols;
      u32 const row = view_index / batch.cols;
      LfdGridCell const logical = LfdLogicalCellForGridPosition(data.lfd_view_order, data.lfd_grid, col, row);
      QuiltView const view = MakeQuiltView(QuiltViewRequest{
        .center = &setup.camera,
        .grid = data.lfd_grid,
        .col = logical.col,
        .row = logical.row,
        .view_cone_deg = data.view_cone_deg,
        .focal_distance = data.lfd_focal_distance,
        .tile_aspect = batch.tile_aspect,
      });
      ApplyQuiltTilePush(data, view, QuiltTileForView(batch, view_index));
      cameras.at(local) = data.project_push;
    }
    u32 const batch_index = batch.begin / batch.size;
    data.gpu_pass_timer.write_view(
      context, data.current_slot, batch_index, GpuPass::kMultiviewProjection, false, command_buffer);
    if (auto recorded = DispatchProjectionBatch(
          context, data, std::span<ProjectPushConstants const>{ cameras.data(), batch.count }, command_buffer);
      !recorded) {
      return recorded;
    }
    data.gpu_pass_timer.write_view(
      context, data.current_slot, batch_index, GpuPass::kMultiviewProjection, true, command_buffer);
    return {};
  }

  [[nodiscard]] auto RecordQuiltBatchTiles(vulkan::Context &context,
    RenderData &data,
    VkCommandBuffer command_buffer,
    FrameSetup const &setup,
    QuiltBatch const &batch) -> std::expected<void, Error>
  {
    for (u32 local = 0U; local < batch.count; ++local) {
      u32 const view_index = batch.begin + local;
      u32 const col = view_index % batch.cols;
      u32 const row = view_index / batch.cols;
      bool const first_tile = view_index == 0U;
      if (auto recorded = RecordQuiltCell(context,
            data,
            command_buffer,
            setup,
            QuiltTileForView(batch, view_index),
            batch.tile_aspect,
            col,
            row,
            first_tile,
            batch.size == 1U,
            batch.size == 1U || local == 0U,
            batch.size == 1U ? 0U : local);
        !recorded) {
        return recorded;
      }
    }
    return {};
  }

  [[nodiscard]] auto RecordQuilt(vulkan::Context &context, RenderData &data, VkCommandBuffer command_buffer)
    -> std::expected<void, Error>
  {
    FrameSetup const &setup = FrameSetupFor(data, data.current_slot);
    u32 const cols = std::max(1U, data.lfd_grid.at(0));
    u32 const rows = std::max(1U, data.lfd_grid.at(1));
    u32 const tile_w = data.quilt_tile_extent.width != 0U ? data.quilt_tile_extent.width : data.output_extent.width;
    u32 const tile_h = data.quilt_tile_extent.height != 0U ? data.quilt_tile_extent.height : data.output_extent.height;
    f64 const tile_aspect = static_cast<f64>(tile_w) / static_cast<f64>(std::max(1U, tile_h));

    if (auto recorded = BeginColorTargetFrame(context, data, command_buffer); !recorded) { return recorded; }
    u32 const view_count = cols * rows;
    u32 const batch_size = data.project_batch_algorithm ? data.lfd_projection_batch_size : 1U;
    for (u32 begin = 0U; begin < view_count; begin += batch_size) {
      QuiltBatch const batch{ .cols = cols,
        .tile_width = tile_w,
        .tile_height = tile_h,
        .begin = begin,
        .count = std::min(batch_size, view_count - begin),
        .size = batch_size,
        .tile_aspect = tile_aspect };
      if (auto recorded = RecordQuiltBatchProjection(context, data, command_buffer, setup, batch); !recorded) {
        return recorded;
      }
      if (auto recorded = RecordQuiltBatchTiles(context, data, command_buffer, setup, batch); !recorded) {
        return recorded;
      }
    }
    if (auto finished = FinishColorTargetFrame(context, data, command_buffer); !finished) { return finished; }
    return BlitQuiltToSwapchain(context, data, command_buffer, data.present_image_index);
  }

}// namespace

void RecordGsPipeline(RenderData &data)
{
  (void)data;
  // Phase A is recorded each frame in EvalGsPipeline (no retained Sequence).
}

auto EvalGsPipeline(vulkan::Context &context, RenderData &data, VkCommandBuffer command_buffer)
  -> std::expected<void, Error>
{
  if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.begin_frame(context, data.current_slot, command_buffer); }

  data.project_push.cull_margin = PhaseACullMargin(data);
  if (auto recorded = RecordPhaseACompute(context, data, command_buffer); !recorded) { return recorded; }

  if (IsMonoQuilt(data)) { return RecordRasterization(context, data, command_buffer); }
  if (data.lfd_emulate_active) { return RecordEmulatedView(context, data, command_buffer); }
  return RecordQuilt(context, data, command_buffer);
}

void DestroyGsPipeline(RenderData &data) { (void)data; }

}// namespace vkgsplat::gs
