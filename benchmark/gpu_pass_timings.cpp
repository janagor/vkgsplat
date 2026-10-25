#include "benchmark/gpu_pass_timings.hpp"

#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"
#include "benchmark/gpu_statistics.hpp"
#include "benchmark/render_throughput.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/types.hpp>

#include <benchmark/benchmark.h>// NOLINT(misc-include-cleaner)

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_set>

namespace vkgsplat::benchmark {

// Google Benchmark's umbrella header provides these declarations, but include-cleaner does not associate them with it.
// NOLINTBEGIN(misc-include-cleaner)
namespace {
  constexpr double kMillisecondsToSeconds = 1.0e-3;
  constexpr std::array<char const *, kGpuPassCount> kPassCounterNames{ "projection",
    "binning",
    "prepare_sort",
    "radix_sort",
    "rasterize",
    "multiview_projection",
    "tiled_count",
    "tiled_scan",
    "tiled_emit",
    "tiled_radix_sort",
    "tiled_ranges",
    "tiled_rasterize" };

  auto PublishSummary(::benchmark::State &state, std::string const &name, GpuSummary const &summary) -> void
  {
    if (summary.count == 0U) { return; }
    auto const prefix = "gpu." + name + ".";
    state.counters[prefix + "mean_ms"] = summary.mean;
    state.counters[prefix + "median_ms"] = summary.median;
    state.counters[prefix + "p95_ms"] = summary.p95;
    state.counters[prefix + "stddev_ms"] = summary.standard_deviation;
    state.counters[prefix + "min_ms"] = summary.minimum;
    state.counters[prefix + "max_ms"] = summary.maximum;
    state.counters[prefix + "samples"] = static_cast<double>(summary.count);
  }

  auto RecordBatch(::benchmark::State &state,
    Renderer &renderer,
    Camera const &camera,
    u64 batch_frames,
    GpuStatistics &statistics,
    std::unordered_set<u64> &frame_ids,
    std::atomic_bool &failed,
    RasterizationMode mode) -> bool
  {
    if (auto batch = RenderBatch(renderer, camera, batch_frames); !batch) {
      failed.store(true);
      state.SkipWithError(batch.error().message());
      return false;
    }
    auto samples = renderer.take_gpu_timings();
    if (!samples || samples->size() != batch_frames) {
      failed.store(true);
      state.SkipWithError("GPU timing sample count differs from completed frames");
      return false;
    }
    double gpu_seconds = 0.0;
    for (auto const &sample : *samples) {
      if (!frame_ids.insert(sample.frame_id).second || !IsCompleteGpuSample(sample, mode)) {
        failed.store(true);
        state.SkipWithError("GPU timing sample has duplicate ID, missing pass, or invalid duration");
        return false;
      }
      gpu_seconds += sample.frame_ms * kMillisecondsToSeconds;
      statistics.add(sample);
    }
    state.SetIterationTime(gpu_seconds);
    return true;
  }

  auto PublishStatistics(::benchmark::State &state, GpuStatistics const &statistics) -> void
  {
    state.counters["gpu_timing_enabled"] = 1.0;
    state.counters["gpu.samples"] = static_cast<double>(statistics.frame_count());
    PublishSummary(state, "frame", statistics.frame());
    for (size_t index = 0U; index < kGpuPassCount; ++index) {
      PublishSummary(state, kPassCounterNames.at(index), statistics.pass(static_cast<GpuPass>(index)));
    }
  }

  auto RunGpuPassTimings(::benchmark::State &state,
    Platform &platform,
    BenchmarkProfile const &profile,
    std::filesystem::path const &scene_path,
    Camera const &camera,
    std::atomic_bool &failed,
    RasterizationMode mode) -> void
  {
    auto renderer = Renderer::create(MakeGpuProfilingRendererConfig(profile, scene_path, mode), platform);
    if (!renderer) {
      failed.store(true);
      state.SkipWithError(renderer.error().message());
      return;
    }
    if (auto warmup = RenderBatch(*renderer, camera, kWarmupFrames); !warmup) {
      failed.store(true);
      state.SkipWithError(warmup.error().message());
      return;
    }
    if (auto discarded = renderer->take_gpu_timings(); !discarded || discarded->size() != kWarmupFrames) {
      failed.store(true);
      state.SkipWithError("GPU warmup timing samples are incomplete");
      return;
    }
    if (mode == RasterizationMode::kComputeTiled) {
      auto intersections = renderer->tiled_intersection_count();
      if (!intersections) {
        failed.store(true);
        state.SkipWithError(intersections.error().message());
        return;
      }
      state.counters["gpu.tiled.intersections"] = static_cast<double>(*intersections);
      state.counters["gpu.tiled.scratch_bytes"] = static_cast<double>(renderer->tiled_scratch_bytes());
    }

    auto const batch_frames = static_cast<u64>(state.range(0));
    if (auto reserved = renderer->reserve_gpu_timing_samples(static_cast<size_t>(batch_frames)); !reserved) {
      failed.store(true);
      state.SkipWithError(reserved.error().message());
      return;
    }
    GpuStatistics statistics;
    statistics.reserve(static_cast<size_t>(batch_frames));
    std::unordered_set<u64> frame_ids;
    for (auto iteration : state) {
      static_cast<void>(iteration);
      if (!RecordBatch(state, *renderer, camera, batch_frames, statistics, frame_ids, failed, mode)) { return; }
    }
    if (!HasCompleteGpuStatistics(statistics, mode)) {
      failed.store(true);
      state.SkipWithError("GPU timing statistics have incomplete required pass counts");
      return;
    }
    PublishStatistics(state, statistics);
  }
}// namespace

auto RegisterGpuPassTimings(Platform &platform,
  BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  Camera const &camera,
  std::atomic_bool &failed) -> void
{
  for (auto const mode : { RasterizationMode::kGraphicsPipeline, RasterizationMode::kComputeTiled }) {
    char const *mode_name = mode == RasterizationMode::kGraphicsPipeline ? "graphics" : "compute";
    auto const name = std::format("BM_GpuPassTimings/{}/{}/{}/{}x{}",
      profile.id,
      mode_name,
      profile.scene.gaussian_count,
      profile.width,
      profile.height);
    auto *registered = ::benchmark::RegisterBenchmark(
      name, [&platform, &profile, &scene_path, &camera, &failed, mode](::benchmark::State &state) -> void {
        RunGpuPassTimings(state, platform, profile, scene_path, camera, failed, mode);
      });
    registered->ArgName("batch")->UseManualTime()->Unit(::benchmark::kMillisecond);
    for (auto const batch_frames : kBatchFrameCounts) { registered->Arg(static_cast<std::int64_t>(batch_frames)); }
  }
}
// NOLINTEND(misc-include-cleaner)

}// namespace vkgsplat::benchmark
