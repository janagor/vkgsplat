#ifndef VKGSPLAT_BACKEND_VULKAN_PRESENT_PACER_HPP
#define VKGSPLAT_BACKEND_VULKAN_PRESENT_PACER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace vulkan { struct Context; }

// Schedules vkQueuePresentKHR via VK_EXT_present_timing when available.
class PresentPacer
{
public:
  [[nodiscard]] static auto TryCreate(vulkan::Context &context, FrameRateConfig const &config) -> std::unique_ptr<PresentPacer>;

  PresentPacer(PresentPacer const &) = delete;
  auto operator=(PresentPacer const &) -> PresentPacer & = delete;
  PresentPacer(PresentPacer &&) = delete;
  auto operator=(PresentPacer &&) -> PresentPacer & = delete;
  ~PresentPacer() = default;

  void OnSwapchainRecreated(vulkan::Context &context);

  // Chains present-timing (+ present-id2) onto present_info.pNext. Storage is owned by this object
  // until the next PreparePresent / AfterPresent call.
  void PreparePresent(vulkan::Context const &context, VkPresentInfoKHR &present_info);

  void AfterPresent(vulkan::Context &context);

  [[nodiscard]] auto Active() const noexcept -> bool { return active_; }
  [[nodiscard]] auto TargetPeriodNs() const noexcept -> u64 { return target_period_ns_; }

private:
  PresentPacer(vulkan::Context &context, FrameRateConfig config);

  void RefreshTimingProperties(vulkan::Context &context);
  void RefreshTimeDomain(vulkan::Context &context);
  static void EnsureTimingQueue(vulkan::Context &context);
  void UpdateDisplayTargetPeriod();
  void UpdateAdaptiveTargetPeriod();
  void UpdateTargetPeriodFromFeedback();
  [[nodiscard]] auto ComputeTargetTime() const -> u64;
  void DrainPastTimings(vulkan::Context &context);

  FrameRateConfig config_{};
  bool active_ = false;
  bool use_absolute_time_ = false;
  bool use_present_id2_ = false;
  VkPresentStageFlagsEXT present_stage_queries_ = 0;

  u64 refresh_duration_ns_ = 0;
  u64 refresh_interval_ns_ = 0;
  // Mesa (and possibly other drivers) use timeDomainId == 0 as a valid id.
  bool has_time_domain_ = false;
  u64 time_domain_id_ = 0;
  u64 timing_properties_counter_ = 0;
  u64 time_domains_counter_ = 0;

  u64 target_period_ns_ = 0;
  u64 next_present_id_ = 1;
  u64 last_result_present_id_ = 0;
  u64 last_result_present_time_ = 0;

  f64 frame_duration_ema_ns_ = 0.0;
  u32 over_budget_streak_ = 0;
  u32 under_budget_streak_ = 0;
  u32 adaptive_frames_until_recompute_ = 0;

  static constexpr u32 kMaxTimingResults = 8;
  static constexpr u32 kMaxStagesPerResult = 4;
  static constexpr size_t kStageTimeStorageSize =
    static_cast<size_t>(kMaxTimingResults) * static_cast<size_t>(kMaxStagesPerResult);

  std::array<VkPastPresentationTimingEXT, kMaxTimingResults> past_timings_{};
  std::array<VkPresentStageTimeEXT, kStageTimeStorageSize> stage_times_{};

  // Storage for the next present call (must stay alive through queuePresentKHR).
  u64 present_id_storage_ = 0;
  VkPresentId2KHR present_id_info_{};
  VkPresentTimingInfoEXT timing_info_{};
  VkPresentTimingsInfoEXT timings_info_{};
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_PRESENT_PACER_HPP
