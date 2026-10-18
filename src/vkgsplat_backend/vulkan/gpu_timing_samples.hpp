#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_TIMING_SAMPLES_HPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_TIMING_SAMPLES_HPP

#include <vkgsplat/gpu_timings.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace vkgsplat {

// RenderThread guards this buffer with its control mutex.
class GpuTimingSamples
{
public:
  auto reserve(size_t count) -> void
  {
    reserved_capacity_ = std::max(reserved_capacity_, count);
    pending_.reserve(reserved_capacity_);
  }

  auto publish(GpuFrameTimings sample) -> void { pending_.push_back(sample); }

  [[nodiscard]] auto take() -> std::vector<GpuFrameTimings>
  {
    if (pending_.empty()) { return {}; }
    std::vector<GpuFrameTimings> samples;
    samples.reserve(reserved_capacity_);
    samples.swap(pending_);
    std::ranges::sort(samples, {}, &GpuFrameTimings::frame_id);
    return samples;
  }

  auto reset() -> void
  {
    pending_ = std::vector<GpuFrameTimings>{};
    reserved_capacity_ = 0U;
  }

private:
  size_t reserved_capacity_{};
  std::vector<GpuFrameTimings> pending_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_TIMING_SAMPLES_HPP
