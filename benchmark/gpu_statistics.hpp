#ifndef VKGSPLAT_BENCHMARK_GPU_STATISTICS_HPP
#define VKGSPLAT_BENCHMARK_GPU_STATISTICS_HPP

#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/renderer.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace vkgsplat::benchmark {

struct GpuSummary
{
  size_t count{};
  double mean{};
  double median{};
  double p95{};
  double standard_deviation{};
  double minimum{};
  double maximum{};
};

// Percentiles use nearest rank. Standard deviation is population deviation.
[[nodiscard]] auto SummarizeGpuValues(std::vector<double> values) -> GpuSummary;
[[nodiscard]] auto IsCompleteMonoGpuSample(GpuFrameTimings const &sample) -> bool;
[[nodiscard]] auto IsCompleteGpuSample(GpuFrameTimings const &sample, RasterizationMode mode) -> bool;

class GpuStatistics
{
public:
  auto reserve(size_t sample_capacity) -> void;
  auto add(GpuFrameTimings const &sample) -> void;
  [[nodiscard]] auto frame() const -> GpuSummary;
  [[nodiscard]] auto pass(GpuPass pass) const -> GpuSummary;
  [[nodiscard]] auto frame_count() const noexcept -> size_t { return frame_ms_.size(); }
  [[nodiscard]] auto pass_count(GpuPass selected_pass) const -> size_t;

private:
  std::vector<double> frame_ms_;
  std::array<std::vector<double>, kGpuPassCount> pass_ms_;
};

[[nodiscard]] auto HasCompleteMonoGpuStatistics(GpuStatistics const &statistics) -> bool;
[[nodiscard]] auto HasCompleteGpuStatistics(GpuStatistics const &statistics, RasterizationMode mode) -> bool;

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_GPU_STATISTICS_HPP
