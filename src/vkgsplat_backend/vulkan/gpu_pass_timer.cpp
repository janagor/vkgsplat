#include "vulkan/gpu_pass_timer.hpp"

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <algorithm>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace {

  constexpr u32 kQueriesPerPass = 2U;
  constexpr u32 kViewPassCount = 2U;
  constexpr u32 kBasePassCount = static_cast<u32>(GpuPass::kMultiviewProjection);
  constexpr u32 kBaseQueriesPerSlot = kBasePassCount * kQueriesPerPass;
  constexpr u32 kQueriesPerView = kViewPassCount * kQueriesPerPass;
  constexpr u32 kResultValuesPerQuery = 2U;
  constexpr float kNsToMs = 1.0e-6F;

  [[nodiscard]] auto ReadRange(std::vector<u64> const &results,
    size_t begin_query,// NOLINT(bugprone-easily-swappable-parameters)
    float scale) -> std::optional<float>
  {
    size_t const begin_offset = begin_query * kResultValuesPerQuery;
    size_t const end_offset = (begin_query + 1U) * kResultValuesPerQuery;
    if (results.at(begin_offset + 1U) == 0U || results.at(end_offset + 1U) == 0U) { return std::nullopt; }
    u64 const begin_ticks = results.at(begin_offset);
    u64 const end_ticks = results.at(end_offset);
    if (end_ticks < begin_ticks) { return 0.0F; }
    return static_cast<float>(end_ticks - begin_ticks) * scale;
  }

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
  case GpuPass::kCount:
    break;
  }
  return "?";
}

GpuPassTimer::GpuPassTimer(GpuPassTimer &&other) noexcept
  : pool_(std::exchange(other.pool_, VK_NULL_HANDLE)), timestamp_period_ns_(other.timestamp_period_ns_),
    enabled_(other.enabled_), pending_(other.pending_), last_ms_(other.last_ms_), view_count_(other.view_count_),
    queries_per_slot_(other.queries_per_slot_), recorded_base_passes_(other.recorded_base_passes_),
    recorded_view_projections_(other.recorded_view_projections_), recorded_view_rasters_(other.recorded_view_rasters_),
    query_results_(std::move(other.query_results_))
{}

auto GpuPassTimer::operator=(GpuPassTimer &&other) noexcept -> GpuPassTimer &
{
  if (this == &other) { return *this; }
  // Caller must destroy an existing pool before move-assigning over a live timer.
  pool_ = std::exchange(other.pool_, VK_NULL_HANDLE);
  timestamp_period_ns_ = other.timestamp_period_ns_;
  enabled_ = other.enabled_;
  pending_ = other.pending_;
  last_ms_ = other.last_ms_;
  view_count_ = other.view_count_;
  queries_per_slot_ = other.queries_per_slot_;
  recorded_base_passes_ = other.recorded_base_passes_;
  recorded_view_projections_ = other.recorded_view_projections_;
  recorded_view_rasters_ = other.recorded_view_rasters_;
  query_results_ = std::move(other.query_results_);
  return *this;
}

auto GpuPassTimer::query_index(size_t slot, GpuPass pass, bool is_end) const noexcept -> u32
{ return (static_cast<u32>(slot) * queries_per_slot_) + (static_cast<u32>(pass) * 2U) + (is_end ? 1U : 0U); }

auto GpuPassTimer::create(vulkan::Context &context, u32 view_count) -> bool
{
  destroy(context);

  constexpr u32 kMaxViewCount =
    ((std::numeric_limits<u32>::max() / static_cast<u32>(kFrameSlotCount)) - kBaseQueriesPerSlot) / kQueriesPerView;
  if (view_count > kMaxViewCount) { return false; }
  view_count_ = view_count;
  queries_per_slot_ = kBaseQueriesPerSlot + (view_count_ * kQueriesPerView);

  timestamp_period_ns_ = context.device.physical_device.properties.limits.timestampPeriod;
  if (timestamp_period_ns_ <= 0.0F) { return false; }

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
  recorded_base_passes_.fill({});
  recorded_view_projections_.fill(0U);
  recorded_view_rasters_.fill(0U);
  for (auto &results : query_results_) {
    results.resize(static_cast<size_t>(queries_per_slot_) * kResultValuesPerQuery);
  }
  last_ms_.fill(0.0F);
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
  recorded_view_projections_.fill(0U);
  recorded_view_rasters_.fill(0U);
  for (auto &results : query_results_) { results.clear(); }
  last_ms_.fill(0.0F);
}

auto GpuPassTimer::begin_frame(vulkan::Context const &context, size_t slot, VkCommandBuffer command_buffer) -> void
{
  if (!enabled()) { return; }
  recorded_base_passes_.at(slot).fill(false);
  recorded_view_projections_.at(slot) = 0U;
  recorded_view_rasters_.at(slot) = 0U;
  u32 const first = static_cast<u32>(slot) * queries_per_slot_;
  context.disp.cmdResetQueryPool(command_buffer, pool_, first, queries_per_slot_);
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
  if (is_end && pass == GpuPass::kMultiviewProjection) { recorded_view_projections_.at(slot) = view_index + 1U; }
  if (is_end && pass == GpuPass::kRasterize) { recorded_view_rasters_.at(slot) = view_index + 1U; }
}

void GpuPassTimer::mark_submitted(size_t slot)
{
  if (!enabled()) { return; }
  if (slot < pending_.size()) { pending_.at(slot) = true; }
}

void GpuPassTimer::resolve(vulkan::Context const &context, size_t slot)
{
  if (!enabled() || slot >= pending_.size() || !pending_.at(slot)) { return; }

  auto &results = query_results_.at(slot);
  u32 const first = static_cast<u32>(slot) * queries_per_slot_;
  VkResult const result = context.disp.getQueryPoolResults(pool_,
    first,
    queries_per_slot_,
    results.size() * sizeof(u64),
    results.data(),
    kResultValuesPerQuery * sizeof(u64),
    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if (result != VK_SUCCESS && result != VK_NOT_READY) { return; }

  std::array<float, kGpuPassCount> next_ms{};
  float const scale = timestamp_period_ns_ * kNsToMs;
  for (u32 pass_index = 0; pass_index < kBasePassCount; ++pass_index) {
    if (!recorded_base_passes_.at(slot).at(pass_index)) { continue; }
    auto const pass_ms = ReadRange(results, static_cast<size_t>(pass_index) * kQueriesPerPass, scale);
    if (!pass_ms) { return; }
    next_ms.at(pass_index) = *pass_ms;
  }
  for (u32 view = 0U; view < recorded_view_projections_.at(slot); ++view) {
    size_t const begin_query = kBaseQueriesPerSlot + (static_cast<size_t>(view) * kQueriesPerView);
    auto const pass_ms = ReadRange(results, begin_query, scale);
    if (!pass_ms) { return; }
    next_ms.at(static_cast<size_t>(GpuPass::kMultiviewProjection)) += *pass_ms;
  }
  for (u32 view = 0U; view < recorded_view_rasters_.at(slot); ++view) {
    size_t const begin_query = kBaseQueriesPerSlot + (static_cast<size_t>(view) * kQueriesPerView) + kQueriesPerPass;
    auto const pass_ms = ReadRange(results, begin_query, scale);
    if (!pass_ms) { return; }
    next_ms.at(static_cast<size_t>(GpuPass::kRasterize)) += *pass_ms;
  }
  last_ms_ = next_ms;
  pending_.at(slot) = false;
}

auto GpuPassTimer::total_ms() const noexcept -> float { return std::ranges::fold_left(last_ms_, 0.0F, std::plus<>{}); }

}// namespace vkgsplat
