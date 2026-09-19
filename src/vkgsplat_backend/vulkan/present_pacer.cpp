#include "present_pacer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <vector>

#include "vulkan_context.hpp"
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace {

  constexpr u64 kNsPerSecond = 1'000'000'000ULL;
  constexpr u32 kFallbackDisplayFps = 60;
  constexpr f64 kEmaAlpha = 0.15;
  constexpr f64 kSustainRatio = 0.95;
  constexpr f64 kHeadroomRatio = 0.80;
  constexpr f64 kAdaptiveSafety = 1.05;
  constexpr u32 kStreakFrames = 8;
  constexpr u32 kAdaptiveRecomputeFrames = 30;
  constexpr u32 kMaxAdaptiveMultiples = 8;
  constexpr u32 kMinTimingQueueSize = 4;

  [[nodiscard]] auto PeriodFromFps(u32 fps) -> u64
  {
    if (fps == 0U) { return 0; }
    return kNsPerSecond / static_cast<u64>(fps);
  }

  [[nodiscard]] auto FindStageTime(VkPastPresentationTimingEXT const &timing, VkPresentStageFlagsEXT stage) -> u64
  {
    if (timing.pPresentStages == nullptr || timing.presentStageCount == 0U) { return 0; }
    auto const stages = std::span{ timing.pPresentStages, timing.presentStageCount };
    auto const found = std::ranges::find_if(
      stages, [stage](VkPresentStageTimeEXT const &entry) -> bool { return (entry.stage & stage) != 0U; });
    if (found != stages.end()) { return found->time; }
    return stages.front().time;
  }

}// namespace

auto PresentPacer::TryCreate(vulkan::Context &context, FrameRateConfig const &config) -> std::unique_ptr<PresentPacer>
{
  if (!config.IsPacingRequested()) { return nullptr; }

  if (!context.present_timing_enabled || context.swapchain == nullptr || !context.swapchain->present_timing_enabled()) {
    std::println(stderr, "[present-timing] pacing requested but not available; running uncapped");
    return nullptr;
  }

  auto pacer = std::unique_ptr<PresentPacer>(new PresentPacer(context, config));
  if (!pacer->Active()) { return nullptr; }
  return pacer;
}

PresentPacer::PresentPacer(vulkan::Context &context, FrameRateConfig config)
  : config_(config), use_absolute_time_(context.present_at_absolute_time),
    use_present_id2_(context.present_id2_enabled), present_stage_queries_(context.present_stage_queries)
{
  if (!use_absolute_time_ && !context.present_at_relative_time) {
    std::println(stderr, "[present-timing] neither absolute nor relative present-at-time supported");
    active_ = false;
    return;
  }

  for (u32 i = 0; i < kMaxTimingResults; ++i) {
    past_timings_.at(i).sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT;
    past_timings_.at(i).pNext = nullptr;
    size_t const stage_offset = static_cast<size_t>(i) * static_cast<size_t>(kMaxStagesPerResult);
    past_timings_.at(i).pPresentStages = &stage_times_.at(stage_offset);
  }

  OnSwapchainRecreated(context);

  if (config_.mode == FrameRateMode::kFixed) {
    target_period_ns_ = PeriodFromFps(config_.fixed_fps);
  } else if (refresh_duration_ns_ > 0U) {
    target_period_ns_ = refresh_duration_ns_;
  } else {
    target_period_ns_ = PeriodFromFps(kFallbackDisplayFps);
  }

  // Both absolute and relative PresentTimingInfo need a swapchain timeDomainId
  // (id 0 is valid — Mesa returns that).
  active_ = target_period_ns_ != 0U && has_time_domain_;
  if (!active_) {
    std::println(stderr,
      "[present-timing] could not initialize pacing (hasTimeDomain={}, timeDomainId={}, period={} ns); running "
      "uncapped",
      has_time_domain_,
      time_domain_id_,
      target_period_ns_);
  } else {
    std::println("[present-timing] pacing active (mode={}, period={} ns, absolute={}, presentId2={})",
      static_cast<unsigned>(config_.mode),
      target_period_ns_,
      use_absolute_time_,
      use_present_id2_);
  }
}

void PresentPacer::OnSwapchainRecreated(vulkan::Context &context)
{
  EnsureTimingQueue(context);
  RefreshTimingProperties(context);
  RefreshTimeDomain(context);

  if (config_.mode == FrameRateMode::kFixed) {
    target_period_ns_ = PeriodFromFps(config_.fixed_fps);
  } else if (refresh_duration_ns_ > 0U) {
    target_period_ns_ = refresh_duration_ns_;
  }

  last_result_present_id_ = 0;
  last_result_present_time_ = 0;
  over_budget_streak_ = 0;
  under_budget_streak_ = 0;
  adaptive_frames_until_recompute_ = 0;
}

void PresentPacer::EnsureTimingQueue(vulkan::Context &context)
{
  if (context.set_swapchain_present_timing_queue_size == nullptr || context.swapchain == nullptr) { return; }
  u32 const queue_size = std::max(kMinTimingQueueSize, context.swapchain->image_count() * 2U);
  static_cast<void>(
    context.set_swapchain_present_timing_queue_size(context.device, context.swapchain->handle(), queue_size));
}

void PresentPacer::RefreshTimingProperties(vulkan::Context &context)
{
  if (context.get_swapchain_timing_properties == nullptr || context.swapchain == nullptr) { return; }

  VkSwapchainTimingPropertiesEXT props{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT,
    .pNext = nullptr,
    .refreshDuration = 0,
    .refreshInterval = 0,
  };
  u64 counter = 0;
  if (context.get_swapchain_timing_properties(context.device, context.swapchain->handle(), &props, &counter)
      != VK_SUCCESS) {
    return;
  }

  refresh_duration_ns_ = props.refreshDuration;
  refresh_interval_ns_ = props.refreshInterval;
  timing_properties_counter_ = counter;
}

void PresentPacer::RefreshTimeDomain(vulkan::Context &context)
{
  has_time_domain_ = false;
  time_domain_id_ = 0;
  if (context.get_swapchain_time_domain_properties == nullptr || context.swapchain == nullptr) { return; }

  VkSwapchainTimeDomainPropertiesEXT props{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT,
    .pNext = nullptr,
    .timeDomainCount = 0,
    .pTimeDomains = nullptr,
    .pTimeDomainIds = nullptr,
  };

  VkResult count_result =
    context.get_swapchain_time_domain_properties(context.device, context.swapchain->handle(), &props, nullptr);
  if (count_result != VK_SUCCESS && count_result != VK_INCOMPLETE) {
    std::println(
      stderr, "[present-timing] GetSwapchainTimeDomainPropertiesEXT count failed ({})", static_cast<int>(count_result));
    return;
  }
  if (props.timeDomainCount == 0U) {
    std::println(stderr, "[present-timing] no swapchain time domains reported");
    return;
  }

  std::vector<VkTimeDomainKHR> domains(props.timeDomainCount);
  std::vector<u64> ids(props.timeDomainCount);
  props.pTimeDomains = domains.data();
  props.pTimeDomainIds = ids.data();

  u64 counter = 0;
  VkResult const fill_result =
    context.get_swapchain_time_domain_properties(context.device, context.swapchain->handle(), &props, &counter);
  if (fill_result != VK_SUCCESS && fill_result != VK_INCOMPLETE) {
    std::println(
      stderr, "[present-timing] GetSwapchainTimeDomainPropertiesEXT fill failed ({})", static_cast<int>(fill_result));
    return;
  }

  time_domains_counter_ = counter;
  u32 chosen = 0;
  for (u32 i = 0; i < props.timeDomainCount; ++i) {
    if (domains.at(i) == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT
        || domains.at(i) == VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT) {
      chosen = i;
      break;
    }
  }
  time_domain_id_ = ids.at(chosen);
  has_time_domain_ = true;
}

void PresentPacer::UpdateDisplayTargetPeriod()
{
  f64 const cost = frame_duration_ema_ns_;
  if (cost <= 0.0 || refresh_duration_ns_ == 0U) { return; }

  if (cost > static_cast<f64>(refresh_duration_ns_) * kSustainRatio) {
    ++over_budget_streak_;
    under_budget_streak_ = 0;
  } else if (cost < static_cast<f64>(refresh_duration_ns_) * kHeadroomRatio) {
    ++under_budget_streak_;
    over_budget_streak_ = 0;
  } else {
    over_budget_streak_ = 0;
    under_budget_streak_ = 0;
  }

  if (over_budget_streak_ >= kStreakFrames) {
    target_period_ns_ = refresh_duration_ns_ * 2U;
    over_budget_streak_ = 0;
  } else if (under_budget_streak_ >= kStreakFrames * 2U && target_period_ns_ > refresh_duration_ns_) {
    target_period_ns_ = refresh_duration_ns_;
    under_budget_streak_ = 0;
  }
}

void PresentPacer::UpdateAdaptiveTargetPeriod()
{
  if (adaptive_frames_until_recompute_ > 0U) {
    --adaptive_frames_until_recompute_;
    return;
  }
  adaptive_frames_until_recompute_ = kAdaptiveRecomputeFrames;

  f64 const needed = frame_duration_ema_ns_ * kAdaptiveSafety;
  if (needed <= 0.0 || refresh_duration_ns_ == 0U) { return; }

  u64 const quanta =
    refresh_interval_ns_ == 0U || refresh_interval_ns_ == UINT64_MAX ? refresh_duration_ns_ : refresh_interval_ns_;
  if (quanta == 0U) { return; }

  u64 chosen = refresh_duration_ns_;
  for (u32 multiple = 1; multiple <= kMaxAdaptiveMultiples; ++multiple) {
    u64 const period = std::max(quanta * multiple, refresh_duration_ns_);
    chosen = period;
    if (static_cast<f64>(period) >= needed) { break; }
  }
  target_period_ns_ = chosen;
}

void PresentPacer::UpdateTargetPeriodFromFeedback()
{
  if (config_.mode == FrameRateMode::kDisplay) {
    UpdateDisplayTargetPeriod();
    return;
  }
  if (config_.mode == FrameRateMode::kAdaptive) { UpdateAdaptiveTargetPeriod(); }
}

auto PresentPacer::ComputeTargetTime() const -> u64
{
  if (!use_absolute_time_) { return target_period_ns_; }

  if (last_result_present_time_ == 0U || target_period_ns_ == 0U) { return 0; }

  if (!use_present_id2_) { return last_result_present_time_ + target_period_ns_; }

  u64 const delta_ids = next_present_id_ > last_result_present_id_ ? next_present_id_ - last_result_present_id_ : 1U;
  return last_result_present_time_ + (delta_ids * target_period_ns_);
}

void PresentPacer::PreparePresent(vulkan::Context const &context, VkPresentInfoKHR &present_info)
{
  if (!active_ || context.swapchain == nullptr) { return; }

  void const *chain_tail = nullptr;
  if (use_present_id2_) {
    present_id_storage_ = next_present_id_;
    present_id_info_ = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR,
      .pNext = nullptr,
      .swapchainCount = 1,
      .pPresentIds = &present_id_storage_,
    };
    chain_tail = &present_id_info_;
  }

  VkPresentTimingInfoFlagsEXT flags = 0;
  if (!use_absolute_time_) { flags |= VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT; }
  flags |= VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;

  VkPresentStageFlagsEXT stage_queries = present_stage_queries_;
  if (stage_queries == 0U) { stage_queries = VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT; }
  VkPresentStageFlagsEXT target_stage = VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT;
  if ((stage_queries & target_stage) == 0U) {
    // Fall back to any reported stage (e.g. Xwayland only exposes REQUEST_DEQUEUED).
    target_stage = static_cast<VkPresentStageFlagsEXT>(stage_queries & ~(stage_queries - 1U));
  }

  timing_info_ = {
    .sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT,
    .pNext = nullptr,
    .flags = flags,
    .targetTime = ComputeTargetTime(),
    .timeDomainId = time_domain_id_,
    .presentStageQueries = stage_queries,
    .targetTimeDomainPresentStage = target_stage,
  };

  timings_info_ = {
    .sType = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT,
    .pNext = chain_tail,
    .swapchainCount = 1,
    .pTimingInfos = &timing_info_,
  };

  present_info.pNext = &timings_info_;
  ++next_present_id_;
}

void PresentPacer::DrainPastTimings(vulkan::Context &context)
{
  if (context.get_past_presentation_timing == nullptr || context.swapchain == nullptr) { return; }

  VkPastPresentationTimingInfoEXT const past_info{
    .sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT,
    .pNext = nullptr,
    .flags = VK_PAST_PRESENTATION_TIMING_ALLOW_PARTIAL_RESULTS_BIT_EXT,
    .swapchain = context.swapchain->handle(),
  };

  VkPastPresentationTimingPropertiesEXT past_props{
    .sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT,
    .pNext = nullptr,
    .timingPropertiesCounter = 0,
    .timeDomainsCounter = 0,
    .presentationTimingCount = kMaxTimingResults,
    .pPresentationTimings = past_timings_.data(),
  };

  if (context.get_past_presentation_timing(context.device, &past_info, &past_props) != VK_SUCCESS) { return; }

  if (past_props.timingPropertiesCounter != timing_properties_counter_) { RefreshTimingProperties(context); }
  if (past_props.timeDomainsCounter != time_domains_counter_) { RefreshTimeDomain(context); }

  std::optional<u64> previous_visible{};
  for (u32 i = 0; i < past_props.presentationTimingCount; ++i) {
    auto const &timing = past_timings_.at(i);
    if (timing.reportComplete != VK_TRUE) { continue; }

    u64 const visible = FindStageTime(timing, VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT);
    if (visible == 0U) { continue; }

    if (previous_visible.has_value() && visible > *previous_visible) {
      f64 const delta = static_cast<f64>(visible - *previous_visible);
      frame_duration_ema_ns_ =
        frame_duration_ema_ns_ > 0.0 ? ((1.0 - kEmaAlpha) * frame_duration_ema_ns_) + (kEmaAlpha * delta) : delta;
    }
    previous_visible = visible;

    if (timing.presentId >= last_result_present_id_) {
      last_result_present_id_ = timing.presentId;
      last_result_present_time_ = visible;
    }
  }

  UpdateTargetPeriodFromFeedback();
}

void PresentPacer::AfterPresent(vulkan::Context &context)
{
  if (!active_) { return; }
  DrainPastTimings(context);
}

}// namespace vkgsplat
