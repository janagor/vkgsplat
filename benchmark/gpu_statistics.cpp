#include "benchmark/gpu_statistics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/renderer.hpp>

namespace vkgsplat::benchmark {

namespace {
  constexpr double kP95Fraction = 0.95;
  constexpr size_t kMedianDivisor = 2U;
  constexpr double kZeroMilliseconds = 0.0;
  constexpr std::array<GpuPass, 6> kGraphicsPipelinePasses{ GpuPass::kProjection,
    GpuPass::kBinning,
    GpuPass::kPrepareSort,
    GpuPass::kRadixSort,
    GpuPass::kRasterize,
    GpuPass::kMultiviewProjection };
  constexpr std::array<GpuPass, 7> kComputePasses{ GpuPass::kProjection,
    GpuPass::kTiledCount,
    GpuPass::kTiledScan,
    GpuPass::kTiledEmit,
    GpuPass::kTiledRadixSort,
    GpuPass::kTiledRanges,
    GpuPass::kTiledRasterize };
}// namespace

auto IsCompleteGpuSample(GpuFrameTimings const &sample, RasterizationMode mode) -> bool
{
  if (!std::isfinite(sample.frame_ms) || sample.frame_ms < kZeroMilliseconds) { return false; }
  for (size_t index = 0U; index < kGpuPassCount; ++index) {
    auto const pass = static_cast<GpuPass>(index);
    bool const required = mode == RasterizationMode::kComputeTiled
                            ? std::ranges::find(kComputePasses, pass) != kComputePasses.end()
                            : std::ranges::find(kGraphicsPipelinePasses, pass) != kGraphicsPipelinePasses.end()
                                && pass != GpuPass::kMultiviewProjection;
    auto const duration = sample.pass_ms.at(index);
    if (required != duration.has_value()) { return false; }
    if (duration && (!std::isfinite(*duration) || *duration < kZeroMilliseconds)) { return false; }
  }
  return true;
}

auto IsCompleteMonoGpuSample(GpuFrameTimings const &sample) -> bool
{
  if (!std::isfinite(sample.frame_ms) || sample.frame_ms < kZeroMilliseconds
      || sample.pass_ms.at(static_cast<size_t>(GpuPass::kMultiviewProjection))) {
    return false;
  }
  for (size_t index = 0U; index < static_cast<size_t>(GpuPass::kMultiviewProjection); ++index) {
    auto const duration = sample.pass_ms.at(index);
    if (!duration.has_value()) { return false; }
    double const value = duration.value_or(kZeroMilliseconds);
    if (!std::isfinite(value) || value < kZeroMilliseconds) { return false; }
  }
  return true;
}

auto SummarizeGpuValues(std::vector<double> values) -> GpuSummary
{
  if (values.empty()) { return {}; }
  std::ranges::sort(values);
  auto const count = values.size();
  double const mean = std::ranges::fold_left(values, kZeroMilliseconds, std::plus<>{}) / static_cast<double>(count);
  double const median = count % kMedianDivisor == 0U
                          ? (values.at((count / kMedianDivisor) - 1U) + values.at(count / kMedianDivisor)) / 2.0
                          : values.at(count / kMedianDivisor);
  size_t const p95_index = static_cast<size_t>(std::ceil(kP95Fraction * static_cast<double>(count))) - 1U;
  double const variance = std::ranges::fold_left(values,
    kZeroMilliseconds,
    [mean](double total, double value) -> double { return total + ((value - mean) * (value - mean)); });
  return GpuSummary{
    .count = count,
    .mean = mean,
    .median = median,
    .p95 = values.at(p95_index),
    .standard_deviation = std::sqrt(variance / static_cast<double>(count)),
    .minimum = values.front(),
    .maximum = values.back(),
  };
}

auto GpuStatistics::reserve(size_t sample_capacity) -> void
{
  frame_ms_.reserve(sample_capacity);
  for (auto &values : pass_ms_) { values.reserve(sample_capacity); }
}

auto GpuStatistics::add(GpuFrameTimings const &sample) -> void
{
  frame_ms_.push_back(sample.frame_ms);
  for (size_t index = 0U; index < kGpuPassCount; ++index) {
    auto const duration = sample.pass_ms.at(index);
    if (duration.has_value()) { pass_ms_.at(index).push_back(duration.value_or(kZeroMilliseconds)); }
  }
}

auto GpuStatistics::frame() const -> GpuSummary { return SummarizeGpuValues(frame_ms_); }

auto GpuStatistics::pass(GpuPass pass) const -> GpuSummary
{ return SummarizeGpuValues(pass_ms_.at(static_cast<size_t>(pass))); }

auto GpuStatistics::pass_count(GpuPass selected_pass) const -> size_t
{ return pass_ms_.at(static_cast<size_t>(selected_pass)).size(); }

auto HasCompleteMonoGpuStatistics(GpuStatistics const &statistics) -> bool
{
  size_t const frame_count = statistics.frame_count();
  if (frame_count == 0U || statistics.pass_count(GpuPass::kMultiviewProjection) != 0U) { return false; }
  for (size_t index = 0U; index < static_cast<size_t>(GpuPass::kMultiviewProjection); ++index) {
    if (statistics.pass_count(static_cast<GpuPass>(index)) != frame_count) { return false; }
  }
  return true;
}

auto HasCompleteGpuStatistics(GpuStatistics const &statistics, RasterizationMode mode) -> bool
{
  size_t const frame_count = statistics.frame_count();
  if (frame_count == 0U) { return false; }
  for (size_t index = 0U; index < kGpuPassCount; ++index) {
    auto const pass = static_cast<GpuPass>(index);
    bool const required = mode == RasterizationMode::kComputeTiled
                            ? std::ranges::find(kComputePasses, pass) != kComputePasses.end()
                            : std::ranges::find(kGraphicsPipelinePasses, pass) != kGraphicsPipelinePasses.end()
                                && pass != GpuPass::kMultiviewProjection;
    if (statistics.pass_count(pass) != (required ? frame_count : 0U)) { return false; }
  }
  return true;
}

}// namespace vkgsplat::benchmark
