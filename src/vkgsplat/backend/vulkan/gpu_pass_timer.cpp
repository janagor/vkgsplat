#include "backend/vulkan/gpu_pass_timer.hpp"

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <algorithm>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <utility>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace {

  constexpr u32 kQueriesPerSlot = k_gpu_pass_count * 2U;
  constexpr u32 kQueryPoolSize = static_cast<u32>(k_max_frames_in_flight) * kQueriesPerSlot;
  constexpr float kNsToMs = 1.0e-6F;

}// namespace

auto gpu_pass_name(GpuPass pass) -> char const *
{
  switch (pass) {
  case GpuPass::Projection:
    return "proj";
  case GpuPass::Binning:
    return "bin";
  case GpuPass::PrepareSort:
    return "prep";
  case GpuPass::RadixSort:
    return "radix";
  case GpuPass::Rasterize:
    return "raster";
  case GpuPass::Count:
    break;
  }
  return "?";
}

GpuPassTimer::GpuPassTimer(GpuPassTimer &&other) noexcept
  : pool_(std::exchange(other.pool_, VK_NULL_HANDLE)), timestamp_period_ns_(other.timestamp_period_ns_),
    enabled_(other.enabled_), pending_(other.pending_), last_ms_(other.last_ms_)
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
  return *this;
}

auto GpuPassTimer::query_index(size_t slot, GpuPass pass, bool is_end) noexcept -> u32
{ return (static_cast<u32>(slot) * kQueriesPerSlot) + (static_cast<u32>(pass) * 2U) + (is_end ? 1U : 0U); }

auto GpuPassTimer::create(Init &init) -> bool
{
  destroy(init);

  timestamp_period_ns_ = init.device.physical_device.properties.limits.timestampPeriod;
  if (timestamp_period_ns_ <= 0.0F) { return false; }

  VkQueryPoolCreateInfo const create_info{
    .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .queryType = VK_QUERY_TYPE_TIMESTAMP,
    .queryCount = kQueryPoolSize,
    .pipelineStatistics = 0,
  };

  if (init.disp.createQueryPool(&create_info, nullptr, &pool_) != VK_SUCCESS) {
    pool_ = VK_NULL_HANDLE;
    return false;
  }

  pending_.fill(false);
  last_ms_.fill(0.0F);
  enabled_ = true;
  return true;
}

void GpuPassTimer::destroy(Init &init)
{
  if (pool_ != VK_NULL_HANDLE) {
    init.disp.destroyQueryPool(pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
  }
  enabled_ = false;
  pending_.fill(false);
  last_ms_.fill(0.0F);
}

void GpuPassTimer::begin_frame(Init const &init, size_t slot, VkCommandBuffer command_buffer) const
{
  if (!enabled()) { return; }
  u32 const first = static_cast<u32>(slot) * kQueriesPerSlot;
  init.disp.cmdResetQueryPool(command_buffer, pool_, first, kQueriesPerSlot);
}

void GpuPassTimer::write(Init const &init, size_t slot, GpuPass pass, bool is_end, VkCommandBuffer command_buffer) const
{
  if (!enabled()) { return; }
  init.disp.cmdWriteTimestamp(
    command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, query_index(slot, pass, is_end));
}

void GpuPassTimer::mark_submitted(size_t slot)
{
  if (!enabled()) { return; }
  if (slot < pending_.size()) { pending_.at(slot) = true; }
}

void GpuPassTimer::resolve(Init const &init, size_t slot)
{
  if (!enabled() || slot >= pending_.size() || !pending_.at(slot)) { return; }

  std::array<u64, kQueriesPerSlot> timestamps{};
  u32 const first = static_cast<u32>(slot) * kQueriesPerSlot;
  VkResult const result = init.disp.getQueryPoolResults(pool_,
    first,
    kQueriesPerSlot,
    timestamps.size() * sizeof(u64),
    timestamps.data(),
    sizeof(u64),
    VK_QUERY_RESULT_64_BIT);

  pending_.at(slot) = false;
  if (result != VK_SUCCESS) { return; }

  for (u32 pass_index = 0; pass_index < k_gpu_pass_count; ++pass_index) {
    size_t const begin_index = static_cast<size_t>(pass_index) * 2U;
    size_t const end_index = begin_index + 1U;
    u64 const begin_ticks = timestamps.at(begin_index);
    u64 const end_ticks = timestamps.at(end_index);
    float pass_ms = 0.0F;
    if (end_ticks >= begin_ticks) {
      pass_ms = static_cast<float>(end_ticks - begin_ticks) * timestamp_period_ns_ * kNsToMs;
    }
    last_ms_.at(pass_index) = pass_ms;
  }
}

auto GpuPassTimer::total_ms() const noexcept -> float { return std::ranges::fold_left(last_ms_, 0.0F, std::plus<>{}); }

}// namespace vkgsplat
