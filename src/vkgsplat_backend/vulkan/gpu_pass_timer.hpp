#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_PASS_TIMER_HPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_PASS_TIMER_HPP

#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace vulkan {
  struct Context;
}

[[nodiscard]] auto GpuPassName(GpuPass pass) -> char const *;

inline constexpr size_t kGpuTimerSlotCount = 2U;

class GpuPassTimer
{
public:
  GpuPassTimer() = default;
  ~GpuPassTimer() = default;

  GpuPassTimer(GpuPassTimer &&other) noexcept;
  auto operator=(GpuPassTimer &&other) noexcept -> GpuPassTimer &;

  GpuPassTimer(GpuPassTimer const &) = delete;
  auto operator=(GpuPassTimer const &) -> GpuPassTimer & = delete;

  [[nodiscard]] auto create(vulkan::Context &context, u32 view_count) -> bool;
  void destroy(vulkan::Context &context);

  void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
  [[nodiscard]] auto enabled() const noexcept -> bool { return enabled_ && pool_ != VK_NULL_HANDLE; }

  auto begin_frame(vulkan::Context const &context, size_t slot, VkCommandBuffer command_buffer) -> void;
  auto
    write(vulkan::Context const &context, size_t slot, GpuPass pass, bool is_end, VkCommandBuffer command_buffer) const
    -> void;
  auto write_view(vulkan::Context const &context,
    size_t slot,
    u32 view_index,
    GpuPass pass,
    bool is_end,
    VkCommandBuffer command_buffer) -> void;
  auto end_frame(vulkan::Context const &context, size_t slot, VkCommandBuffer command_buffer) const -> void;
  void mark_submitted(size_t slot);
  [[nodiscard]] auto resolve(vulkan::Context const &context, size_t slot)
    -> std::expected<std::optional<GpuFrameTimings>, Error>;
  [[nodiscard]] auto timestamp_period_ns() const noexcept -> double { return timestamp_period_ns_; }
  [[nodiscard]] auto timestamp_valid_bits() const noexcept -> u32 { return timestamp_valid_bits_; }

  [[nodiscard]] auto last_ms() const noexcept -> std::array<double, kGpuPassCount> const & { return last_ms_; }
  [[nodiscard]] auto total_ms() const noexcept -> double;

private:
  [[nodiscard]] auto query_index(size_t slot, GpuPass pass, bool is_end) const noexcept -> u32;


  VkQueryPool pool_ = VK_NULL_HANDLE;
  double timestamp_period_ns_ = 0.0;
  u32 timestamp_valid_bits_ = 0U;
  u64 next_frame_id_ = 0U;
  std::array<u64, kGpuTimerSlotCount> pending_frame_ids_{};
  bool enabled_ = false;
  std::array<bool, kGpuTimerSlotCount> pending_{};
  std::array<double, kGpuPassCount> last_ms_{};
  u32 view_count_ = 0U;
  u32 queries_per_slot_ = 0U;
  mutable std::array<std::array<bool, kGpuPassCount>, kGpuTimerSlotCount> recorded_base_passes_{};
  std::array<std::vector<bool>, kGpuTimerSlotCount> recorded_view_projections_{};
  std::array<std::vector<bool>, kGpuTimerSlotCount> recorded_view_rasters_{};
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_PASS_TIMER_HPP
