#include "vulkan/gpu_pass_timer.hpp"

#include "app_state.hpp"
#include "gpu_timestamp_math.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <VkBootstrap.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

static_assert(kGpuTimerSlotCount == kFrameSlotCount);

namespace {

  constexpr u32 kQueriesPerPass = 2U;
  constexpr u32 kViewPassCount = 2U;
  constexpr u32 kBasePassCount = static_cast<u32>(GpuPass::kCount);
  constexpr u32 kBaseQueriesPerSlot = kBasePassCount * kQueriesPerPass;
  constexpr u32 kQueriesPerView = kViewPassCount * kQueriesPerPass;
  constexpr u32 kResultValuesPerQuery = 2U;
  constexpr u32 kFrameQueriesPerSlot = 2U;

}// namespace

auto GpuPassName(GpuPass pass) -> char const *
{
  switch (pass) {
  case GpuPass::kProjection:
    return "proj";
  case GpuPass::kBinning:
    return "bin";
  case GpuPass::kPrepareSort:
    return "prep";
  case GpuPass::kRadixSort:
    return "radix";
  case GpuPass::kRasterize:
    return "raster";
  case GpuPass::kMultiviewProjection:
    return "view proj";
  case GpuPass::kTiledCount: return "tiled count";
  case GpuPass::kTiledScan: return "tiled scan";
  case GpuPass::kTiledEmit: return "tiled emit";
  case GpuPass::kTiledRadixSort: return "tiled radix";
  case GpuPass::kTiledRanges: return "tiled ranges";
  case GpuPass::kTiledRasterize: return "tiled raster";
  case GpuPass::kCount:
    break;
  }
  return "?";
}

GpuPassTimer::GpuPassTimer(GpuPassTimer &&other) noexcept
  : pool_(std::exchange(other.pool_, VK_NULL_HANDLE)), timestamp_period_ns_(other.timestamp_period_ns_),
    timestamp_valid_bits_(other.timestamp_valid_bits_), next_frame_id_(other.next_frame_id_),
    pending_frame_ids_(other.pending_frame_ids_), enabled_(other.enabled_), pending_(other.pending_),
    last_ms_(other.last_ms_), view_count_(other.view_count_), queries_per_slot_(other.queries_per_slot_),
    recorded_base_passes_(other.recorded_base_passes_),
    recorded_view_projections_(std::move(other.recorded_view_projections_)),
    recorded_view_rasters_(std::move(other.recorded_view_rasters_))
{}

auto GpuPassTimer::operator=(GpuPassTimer &&other) noexcept -> GpuPassTimer &
{
  if (this == &other) { return *this; }
  // Caller must destroy an existing pool before move-assigning over a live timer.
  pool_ = std::exchange(other.pool_, VK_NULL_HANDLE);
  timestamp_period_ns_ = other.timestamp_period_ns_;
  timestamp_valid_bits_ = other.timestamp_valid_bits_;
  next_frame_id_ = other.next_frame_id_;
  pending_frame_ids_ = other.pending_frame_ids_;
  enabled_ = other.enabled_;
  pending_ = other.pending_;
  last_ms_ = other.last_ms_;
  view_count_ = other.view_count_;
  queries_per_slot_ = other.queries_per_slot_;
  recorded_base_passes_ = other.recorded_base_passes_;
  recorded_view_projections_ = std::move(other.recorded_view_projections_);
  recorded_view_rasters_ = std::move(other.recorded_view_rasters_);
  return *this;
}

auto GpuPassTimer::query_index(size_t slot, GpuPass pass, bool is_end) const noexcept -> u32
{ return (static_cast<u32>(slot) * queries_per_slot_) + (static_cast<u32>(pass) * 2U) + (is_end ? 1U : 0U); }

auto GpuPassTimer::create(vulkan::Context &context, u32 view_count) -> bool
{
  destroy(context);

  constexpr u32 kMaxViewCount = ((std::numeric_limits<u32>::max() / static_cast<u32>(kFrameSlotCount))
                                  - (kBaseQueriesPerSlot + kFrameQueriesPerSlot))
                                / kQueriesPerView;
  if (view_count > kMaxViewCount) { return false; }
  view_count_ = view_count;
  queries_per_slot_ = kBaseQueriesPerSlot + (view_count_ * kQueriesPerView) + kFrameQueriesPerSlot;

  timestamp_period_ns_ = static_cast<double>(context.device.physical_device.properties.limits.timestampPeriod);
  if (timestamp_period_ns_ <= 0.0) { return false; }
  auto const queue_index = context.device.get_queue_index(vkb::QueueType::graphics);
  if (!queue_index) { return false; }
  auto const queue_properties = context.device.physical_device.get_queue_families();
  if (queue_index.value() >= queue_properties.size()) { return false; }
  timestamp_valid_bits_ = queue_properties.at(queue_index.value()).timestampValidBits;
  if (timestamp_valid_bits_ == 0U) { return false; }

  VkQueryPoolCreateInfo const create_info{
    .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .queryType = VK_QUERY_TYPE_TIMESTAMP,
    .queryCount = static_cast<u32>(kFrameSlotCount) * queries_per_slot_,
    .pipelineStatistics = 0,
  };

  if (context.disp.createQueryPool(&create_info, nullptr, &pool_) != VK_SUCCESS) {
    pool_ = VK_NULL_HANDLE;
    return false;
  }

  pending_.fill(false);
  next_frame_id_ = 0U;
  recorded_base_passes_.fill({});
  for (auto &recorded : recorded_view_projections_) { recorded.assign(view_count_, false); }
  for (auto &recorded : recorded_view_rasters_) { recorded.assign(view_count_, false); }
  last_ms_.fill(0.0);
  enabled_ = true;
  return true;
}

void GpuPassTimer::destroy(vulkan::Context &context)
{
  if (pool_ != VK_NULL_HANDLE) {
    context.disp.destroyQueryPool(pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
  }
  enabled_ = false;
  pending_.fill(false);
  recorded_base_passes_.fill({});
  for (auto &recorded : recorded_view_projections_) { recorded.clear(); }
  for (auto &recorded : recorded_view_rasters_) { recorded.clear(); }
  last_ms_.fill(0.0);
}

auto GpuPassTimer::begin_frame(vulkan::Context const &context, size_t slot, VkCommandBuffer command_buffer) -> void
{
  if (!enabled()) { return; }
  recorded_base_passes_.at(slot).fill(false);
  auto &projections = recorded_view_projections_.at(slot);
  auto &rasters = recorded_view_rasters_.at(slot);
  projections.assign(view_count_, false);
  rasters.assign(view_count_, false);
  u32 const first = static_cast<u32>(slot) * queries_per_slot_;
  context.disp.cmdResetQueryPool(command_buffer, pool_, first, queries_per_slot_);
  context.disp.cmdWriteTimestamp(
    command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, first + queries_per_slot_ - kFrameQueriesPerSlot);
}

auto GpuPassTimer::write(vulkan::Context const &context,
  size_t slot,
  GpuPass pass,
  bool is_end,
  VkCommandBuffer command_buffer) const -> void
{
  assert(static_cast<u32>(pass) < kBasePassCount);
  if (static_cast<u32>(pass) >= kBasePassCount) { return; }
  if (!enabled()) { return; }
  context.disp.cmdWriteTimestamp(
    command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, query_index(slot, pass, is_end));
  if (is_end) { recorded_base_passes_.at(slot).at(static_cast<size_t>(pass)) = true; }
}

auto GpuPassTimer::write_view(vulkan::Context const &context,
  size_t slot,
  u32 view_index,
  GpuPass pass,
  bool is_end,
  VkCommandBuffer command_buffer) -> void
{
  assert(pass == GpuPass::kMultiviewProjection || pass == GpuPass::kRasterize);
  if (pass != GpuPass::kMultiviewProjection && pass != GpuPass::kRasterize) { return; }
  if (!enabled() || view_index >= view_count_) { return; }
  u32 const pass_offset = pass == GpuPass::kRasterize ? kQueriesPerPass : 0U;
  u32 const index = (static_cast<u32>(slot) * queries_per_slot_) + kBaseQueriesPerSlot + (view_index * kQueriesPerView)
                    + pass_offset + (is_end ? 1U : 0U);
  context.disp.cmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, index);
  if (is_end && pass == GpuPass::kMultiviewProjection) { recorded_view_projections_.at(slot).at(view_index) = true; }
  if (is_end && pass == GpuPass::kRasterize) { recorded_view_rasters_.at(slot).at(view_index) = true; }
}

auto GpuPassTimer::end_frame(vulkan::Context const &context, size_t slot, VkCommandBuffer command_buffer) const -> void
{
  if (!enabled()) { return; }
  context.disp.cmdWriteTimestamp(command_buffer,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    pool_,
    (static_cast<u32>(slot) * queries_per_slot_) + queries_per_slot_ - 1U);
}

void GpuPassTimer::mark_submitted(size_t slot)
{
  if (!enabled()) { return; }
  if (slot < pending_.size()) {
    pending_.at(slot) = true;
    pending_frame_ids_.at(slot) = next_frame_id_++;
  }
}

auto GpuPassTimer::resolve(vulkan::Context const &context, size_t slot)
  -> std::expected<std::optional<GpuFrameTimings>, Error>
{
  if (!enabled() || slot >= pending_.size() || !pending_.at(slot)) { return std::nullopt; }

  auto read_pair = [&](size_t begin_query) -> std::expected<double, Error> {
    u32 const first = (static_cast<u32>(slot) * queries_per_slot_) + static_cast<u32>(begin_query);
    std::array<u64, kTimestampPairValueCount> values{};
    VkResult const result = context.disp.getQueryPoolResults(pool_,
      first,
      kQueriesPerPass,
      sizeof(values),
      values.data(),
      kResultValuesPerQuery * sizeof(u64),
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS) {
      return std::unexpected{ MakeError(
        std::errc::io_error, "GPU timestamp query unavailable or failed. VkResult=" + std::to_string(result)) };
    }
    auto const duration = AvailableTimestampPairMs(values, timestamp_valid_bits_, timestamp_period_ns_);
    if (!duration) {
      return std::unexpected{ MakeError(
        std::errc::io_error, "GPU timestamp query unavailable or invalid after completion") };
    }
    return *duration;
  };

  GpuFrameTimings sample{ .frame_id = pending_frame_ids_.at(slot) };
  for (u32 pass_index = 0U; pass_index < kBasePassCount; ++pass_index) {
    if (!recorded_base_passes_.at(slot).at(pass_index)) { continue; }
    auto duration = read_pair(static_cast<size_t>(pass_index) * kQueriesPerPass);
    if (!duration) { return std::unexpected{ duration.error() }; }
    sample.pass_ms.at(pass_index) = *duration;
  }
  for (u32 view = 0U; view < view_count_; ++view) {
    if (!recorded_view_projections_.at(slot).at(view)) { continue; }
    auto duration = read_pair(kBaseQueriesPerSlot + (static_cast<size_t>(view) * kQueriesPerView));
    if (!duration) { return std::unexpected{ duration.error() }; }
    auto &total = sample.pass_ms.at(static_cast<size_t>(GpuPass::kMultiviewProjection));
    total = total.value_or(0.0) + *duration;
  }
  for (u32 view = 0U; view < view_count_; ++view) {
    if (!recorded_view_rasters_.at(slot).at(view)) { continue; }
    auto duration = read_pair(kBaseQueriesPerSlot + (static_cast<size_t>(view) * kQueriesPerView) + kQueriesPerPass);
    if (!duration) { return std::unexpected{ duration.error() }; }
    auto &total = sample.pass_ms.at(static_cast<size_t>(GpuPass::kRasterize));
    total = total.value_or(0.0) + *duration;
  }
  auto frame_duration = read_pair(queries_per_slot_ - kFrameQueriesPerSlot);
  if (!frame_duration) { return std::unexpected{ frame_duration.error() }; }
  sample.frame_ms = *frame_duration;
  for (size_t index = 0U; index < kGpuPassCount; ++index) {
    last_ms_.at(index) = sample.pass_ms.at(index).value_or(0.0);
  }
  pending_.at(slot) = false;
  return std::optional<GpuFrameTimings>{ sample };
}

auto GpuPassTimer::total_ms() const noexcept -> double { return std::ranges::fold_left(last_ms_, 0.0, std::plus<>{}); }

}// namespace vkgsplat
