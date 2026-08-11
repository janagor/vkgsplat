#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_PASS_TIMER_HPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_PASS_TIMER_HPP

#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace vulkan { struct Context; }

enum class GpuPass : u8 {
  kProjection = 0,
  kBinning,
  kPrepareSort,
  kRadixSort,
  kRasterize,
  kCount,
};

inline constexpr u32 kGpuPassCount = static_cast<u32>(GpuPass::kCount);

[[nodiscard]] auto GpuPassName(GpuPass pass) -> char const *;

class GpuPassTimer
{
public:
  GpuPassTimer() = default;
  ~GpuPassTimer() = default;

  GpuPassTimer(GpuPassTimer &&other) noexcept;
  auto operator=(GpuPassTimer &&other) noexcept -> GpuPassTimer &;

  GpuPassTimer(GpuPassTimer const &) = delete;
  auto operator=(GpuPassTimer const &) -> GpuPassTimer & = delete;

  [[nodiscard]] auto create(vulkan::Context &context) -> bool;
  void destroy(vulkan::Context &context);

  void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
  [[nodiscard]] auto enabled() const noexcept -> bool { return enabled_ && pool_ != VK_NULL_HANDLE; }

  void begin_frame(vulkan::Context const &context, size_t slot, VkCommandBuffer command_buffer) const;
  void write(vulkan::Context const &context, size_t slot, GpuPass pass, bool is_end, VkCommandBuffer command_buffer) const;
  void mark_submitted(size_t slot);
  void resolve(vulkan::Context const &context, size_t slot);

  [[nodiscard]] auto last_ms() const noexcept -> std::array<float, kGpuPassCount> const & { return last_ms_; }
  [[nodiscard]] auto total_ms() const noexcept -> float;

private:
  [[nodiscard]] static auto query_index(size_t slot, GpuPass pass, bool is_end) noexcept -> u32;


  VkQueryPool pool_ = VK_NULL_HANDLE;
  float timestamp_period_ns_ = 0.0F;
  bool enabled_ = false;
  std::array<bool, 2> pending_{};
  std::array<float, kGpuPassCount> last_ms_{};
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_PASS_TIMER_HPP
