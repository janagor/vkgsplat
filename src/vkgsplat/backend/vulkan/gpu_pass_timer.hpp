#pragma once

#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

struct Init;

enum class GpuPass : u32 {
  Projection = 0,
  Binning,
  PrepareSort,
  RadixSort,
  Rasterize,
  Count,
};

inline constexpr u32 k_gpu_pass_count = static_cast<u32>(GpuPass::Count);

[[nodiscard]] auto gpu_pass_name(GpuPass pass) -> char const *;

class GpuPassTimer
{
public:
  GpuPassTimer() = default;
  ~GpuPassTimer() = default;

  GpuPassTimer(GpuPassTimer &&other) noexcept;
  auto operator=(GpuPassTimer &&other) noexcept -> GpuPassTimer &;

  GpuPassTimer(GpuPassTimer const &) = delete;
  auto operator=(GpuPassTimer const &) -> GpuPassTimer & = delete;

  [[nodiscard]] auto create(Init &init) -> bool;
  void destroy(Init &init);

  void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
  [[nodiscard]] auto enabled() const noexcept -> bool { return enabled_ && pool_ != VK_NULL_HANDLE; }

  void begin_frame(Init const &init, size_t slot, VkCommandBuffer command_buffer) const;
  void write(Init const &init, size_t slot, GpuPass pass, bool is_end, VkCommandBuffer command_buffer) const;
  void mark_submitted(size_t slot);
  void resolve(Init const &init, size_t slot);

  [[nodiscard]] auto last_ms() const noexcept -> std::array<float, k_gpu_pass_count> const & { return last_ms_; }
  [[nodiscard]] auto total_ms() const noexcept -> float;

private:
  [[nodiscard]] static auto query_index(size_t slot, GpuPass pass, bool is_end) noexcept -> u32;


  VkQueryPool pool_ = VK_NULL_HANDLE;
  float timestamp_period_ns_ = 0.0F;
  bool enabled_ = false;
  std::array<bool, 2> pending_{};
  std::array<float, k_gpu_pass_count> last_ms_{};
};

}// namespace vkgsplat
