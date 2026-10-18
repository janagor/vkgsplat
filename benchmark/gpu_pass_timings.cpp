#include "benchmark/gpu_pass_timings.hpp"

#include "benchmark/fixture/renderer_adapter.hpp"
#include "benchmark/gpu_statistics.hpp"
#include "benchmark/render_throughput.hpp"

#include <benchmark/benchmark.h>
#include <vkgsplat/renderer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_set>

namespace vkgsplat::benchmark {

namespace {
  constexpr double kMillisecondsToSeconds = 1.0e-3;
  constexpr std::array<char const *, kGpuPassCount> kPassCounterNames{ "projection",
    "binning",
    "prepare_sort",
    "radix_sort",
    "rasterize",
    "multiview_projection" };

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
}// namespace

auto RegisterGpuPassTimings(Platform &platform,
  BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  Camera const &camera,
  std::atomic_bool &failed) -> void
{
  auto const name = std::format("BM_GpuPassTimings/{}/{}x{}", kBaselineCount, kBaselineWidth, kBaselineHeight);
  auto *registered = ::benchmark::RegisterBenchmark(
    name.c_str(), [&platform, &profile, &scene_path, &camera, &failed](::benchmark::State &state) -> void {
      auto renderer = Renderer::create(MakeGpuProfilingRendererConfig(profile, scene_path), platform);
      if (!renderer) {
        failed.store(true);
        state.SkipWithError(renderer.error().message().c_str());
        return;
      }
      if (auto warmup = RenderBatch(*renderer, camera, kWarmupFrames); !warmup) {
        failed.store(true);
        state.SkipWithError(warmup.error().message().c_str());
        return;
      }
      if (auto discarded = renderer->take_gpu_timings(); !discarded || discarded->size() != kWarmupFrames) {
        failed.store(true);
        state.SkipWithError("GPU warmup timing samples are incomplete");
        return;
      }

      auto const batch_frames = static_cast<u64>(state.range(0));
      if (auto reserved = renderer->reserve_gpu_timing_samples(static_cast<size_t>(batch_frames)); !reserved) {
        failed.store(true);
        state.SkipWithError(reserved.error().message().c_str());
        return;
      }
      GpuStatistics statistics;
      statistics.reserve(static_cast<size_t>(batch_frames));
      std::unordered_set<u64> frame_ids;
      for (auto _ : state) {
        static_cast<void>(_);
        if (auto batch = RenderBatch(*renderer, camera, batch_frames); !batch) {
          failed.store(true);
          state.SkipWithError(batch.error().message().c_str());
          return;
        }
        auto samples = renderer->take_gpu_timings();
        if (!samples || samples->size() != batch_frames) {
          failed.store(true);
          state.SkipWithError("GPU timing sample count differs from completed frames");
          return;
        }
        double gpu_seconds = 0.0;
        for (auto const &sample : *samples) {
          if (!frame_ids.insert(sample.frame_id).second || !IsCompleteMonoGpuSample(sample)) {
            failed.store(true);
            state.SkipWithError("GPU timing sample has duplicate ID, missing pass, or invalid duration");
            return;
          }
          gpu_seconds += sample.frame_ms * kMillisecondsToSeconds;
          statistics.add(sample);
        }
        state.SetIterationTime(gpu_seconds);
      }
      if (!HasCompleteMonoGpuStatistics(statistics)) {
        failed.store(true);
        state.SkipWithError("GPU timing statistics have incomplete required pass counts");
        return;
      }
      state.counters["gpu_timing_enabled"] = 1.0;
      state.counters["gpu.samples"] = static_cast<double>(statistics.frame_count());
      PublishSummary(state, "frame", statistics.frame());
      for (size_t index = 0U; index < kGpuPassCount; ++index) {
        PublishSummary(state, kPassCounterNames.at(index), statistics.pass(static_cast<GpuPass>(index)));
      }
    });
  registered->ArgName("batch")
    ->Arg(static_cast<std::int64_t>(kBatchFrames))
    ->Arg(static_cast<std::int64_t>(kBatchFrames * 2U))
    ->Arg(static_cast<std::int64_t>(kBatchFrames * 4U))
    ->UseManualTime()
    ->Unit(::benchmark::kMillisecond);
}

}// namespace vkgsplat::benchmark
