#include "benchmark/render_throughput.hpp"
#include "benchmark/batch_accounting.hpp"
#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <benchmark/benchmark.h>// NOLINT(misc-include-cleaner)

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <string>

namespace vkgsplat::benchmark {

auto RenderBatch(Renderer &renderer, Camera const &camera, u64 frame_count) -> std::expected<void, Error>
{
  auto const completed_before = renderer.completed_frame_count();
  auto const recreated_before = renderer.recreated_frame_count();
  for (u64 frame = 0U; frame < frame_count; ++frame) {
    if (auto drawn = renderer.draw(camera); !drawn) { return std::unexpected{ drawn.error() }; }
  }
  if (auto idle = renderer.wait_idle(); !idle) { return std::unexpected{ idle.error() }; }
  return ValidateBatchAccounting(frame_count,
    completed_before,
    renderer.completed_frame_count(),
    recreated_before,
    renderer.recreated_frame_count());
}

// Google Benchmark's umbrella header provides these declarations, but include-cleaner does not associate them with it.
// NOLINTBEGIN(misc-include-cleaner)
auto RegisterRenderThroughput(Platform &platform,
  BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  Camera const &camera,
  std::atomic_bool &failed) -> void
{
  for (auto const mode : { RasterizationMode::kGraphicsPipeline, RasterizationMode::kComputeTiled }) {
    char const *mode_name = mode == RasterizationMode::kGraphicsPipeline ? "graphics" : "compute";
    auto const name = std::format("BM_RenderThroughput/{}/{}/{}/{}x{}", profile.id, mode_name,
      profile.scene.gaussian_count, profile.width, profile.height);
    auto *registered = ::benchmark::RegisterBenchmark(
      name, [&platform, &profile, &scene_path, &camera, &failed, mode](::benchmark::State &state) -> void {
      auto renderer = Renderer::create(MakeRendererConfig(profile, scene_path, mode), platform);
      if (!renderer) {
        failed.store(true);
        state.SkipWithError(renderer.error().message());
        return;
      }
      auto const batch_frames = static_cast<u64>(state.range(0));
      if (auto warmup = RenderBatch(*renderer, camera, kWarmupFrames); !warmup) {
        failed.store(true);
        state.SkipWithError(warmup.error().message());
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

      u64 frames = 0U;
      for (auto iteration : state) {
        static_cast<void>(iteration);
        if (auto batch = RenderBatch(*renderer, camera, batch_frames); !batch) {
          failed.store(true);
          state.SkipWithError(batch.error().message());
          return;
        }
        frames += batch_frames;
      }
      state.counters["gpu_timing_enabled"] = 0.0;
      state.SetItemsProcessed(static_cast<std::int64_t>(frames));
      state.counters["fps"] = ::benchmark::Counter(static_cast<double>(frames), ::benchmark::Counter::kIsRate);
      });
    registered->ArgName("batch")->UseRealTime()->Unit(::benchmark::kMillisecond);
    for (auto const batch_frames : kBatchFrameCounts) { registered->Arg(static_cast<std::int64_t>(batch_frames)); }
  }
}
// NOLINTEND(misc-include-cleaner)

}// namespace vkgsplat::benchmark
