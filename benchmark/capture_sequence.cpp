#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_capture/frame_sequence.hpp>
#include <vkgsplat_capture/screenshot.hpp>
#include <vkgsplat_headless/offscreen_platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <benchmark/benchmark_api.h>
#include <benchmark/counter.h>
#include <benchmark/state.h>
#include <benchmark/types.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <numbers>
#include <string>
#include <system_error>
#include <vector>

namespace vkgsplat::benchmark {
namespace {
  constexpr u64 kCaptureFrames = 120U;
  constexpr u32 kCaptureWidth1080p = 1920U;
  constexpr u32 kCaptureHeight1080p = 1080U;
  constexpr u32 kCaptureWidth4k = 3840U;
  constexpr u32 kCaptureHeight4k = 2160U;
  constexpr f64 kNanosecondsPerMillisecond = 1'000'000.0;
  constexpr f64 kFullCircleRadians = 2.0 * std::numbers::pi_v<f64>;

  enum class CaptureMode : std::uint8_t { kDiscard, kSynchronousPng, kQueuedPng };
  enum class CaptureTrajectory : std::uint8_t { kFixed, kOrbit };

  [[nodiscard]] auto MakeOutputPath() -> std::filesystem::path
  {
    static std::atomic_uint64_t next_path{ 0U };
    auto const tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path()
           / std::format("vkgsplat-capture-benchmark-{}-{}", tick, next_path.fetch_add(1U));
  }

  // GLM exposes named vector components through union members.
  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  [[nodiscard]] auto MakeOrbitCamera(BenchmarkProfile const &profile, u64 index) -> Camera
  {
    auto config = profile.camera;
    auto const horizontal_x = config.position.x - config.target.x;
    auto const horizontal_z = config.position.z - config.target.z;
    auto const angle = static_cast<f64>(index) / static_cast<f64>(kCaptureFrames) * kFullCircleRadians;
    config.position.x = config.target.x + (horizontal_x * std::cos(angle)) + (horizontal_z * std::sin(angle));
    config.position.z = config.target.z - (horizontal_x * std::sin(angle)) + (horizontal_z * std::cos(angle));
    return Camera{ config };
  }
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)

  [[nodiscard]] auto CaptureBatch(Renderer &renderer,
    CaptureMode mode,
    CaptureTrajectory trajectory,
    Camera const &camera,
    std::vector<Camera> const &orbit_cameras,
    std::filesystem::path const &output,
    u64 &queue_wait_nanoseconds,
    size_t &peak_queued_frames) -> std::expected<void, Error>
  {
    if (mode == CaptureMode::kQueuedPng) {
      auto sequence = capture::FrameSequence::create(renderer, { .output_directory = output });
      if (!sequence) { return std::unexpected{ sequence.error() }; }
      for (u64 index = 0U; index < kCaptureFrames; ++index) {
        auto const &capture_camera = trajectory == CaptureTrajectory::kFixed ? camera : orbit_cameras.at(index);
        auto recorded = sequence->record_frame(capture_camera);
        if (!recorded) { return std::unexpected{ recorded.error() }; }
      }
      auto finished = sequence->finish();
      if (!finished) { return std::unexpected{ finished.error() }; }
      if (finished->frame_count != kCaptureFrames) {
        return std::unexpected{ MakeError(std::errc::io_error, "Incomplete sequence") };
      }
      queue_wait_nanoseconds += finished->queue_wait_nanoseconds;
      peak_queued_frames = std::max(peak_queued_frames, finished->peak_queued_frames);
      return {};
    }
    for (u64 index = 0U; index < kCaptureFrames; ++index) {
      auto const &capture_camera = trajectory == CaptureTrajectory::kFixed ? camera : orbit_cameras.at(index);
      auto frame = renderer.render_frame(capture_camera);
      if (!frame) { return std::unexpected{ frame.error() }; }
      if (mode == CaptureMode::kSynchronousPng) {
        auto written = capture::WritePng(*frame, (output / std::format("frame_{:06}.png", index)).string());
        if (!written) { return std::unexpected{ written.error() }; }
      }
    }
    return {};
  }

  auto RunCapture(::benchmark::State &state,
    CaptureMode mode,
    CaptureTrajectory trajectory,
    BenchmarkProfile const &profile,
    std::filesystem::path const &scene_path,
    Camera const &camera,
    std::atomic_bool &failed) -> void
  {
    auto const width = static_cast<u32>(state.range(0));
    auto const height = static_cast<u32>(state.range(1));
    auto platform = OffscreenPlatform::create({ .width = width, .height = height });
    if (!platform) {
      failed.store(true);
      state.SkipWithError(platform.error().message());
      return;
    }
    auto renderer = Renderer::create(MakeRendererConfig(profile, scene_path), *platform);
    if (!renderer) {
      failed.store(true);
      state.SkipWithError(renderer.error().message());
      return;
    }
    std::vector<Camera> orbit_cameras;
    if (trajectory == CaptureTrajectory::kOrbit) {
      orbit_cameras.reserve(kCaptureFrames);
      for (u64 index = 0U; index < kCaptureFrames; ++index) {
        orbit_cameras.push_back(MakeOrbitCamera(profile, index));
      }
    }
    u64 exported = 0U;
    u64 queue_wait_nanoseconds = 0U;
    size_t peak_queued_frames = 0U;
    for (auto iteration : state) {
      static_cast<void>(iteration);
      auto const output = MakeOutputPath();
      if (mode != CaptureMode::kDiscard) { std::filesystem::create_directories(output); }
      auto result = CaptureBatch(
        *renderer, mode, trajectory, camera, orbit_cameras, output, queue_wait_nanoseconds, peak_queued_frames);
      state.PauseTiming();
      if (mode != CaptureMode::kDiscard) { std::filesystem::remove_all(output); }
      state.ResumeTiming();
      if (!result) {
        failed.store(true);
        state.SkipWithError(result.error().message());
        return;
      }
      exported += kCaptureFrames;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(exported));
    state.counters["fps"] = ::benchmark::Counter(static_cast<double>(exported), ::benchmark::Counter::kIsRate);
    if (mode == CaptureMode::kQueuedPng) {
      state.counters["queue_wait_ms_per_batch"] =
        static_cast<f64>(queue_wait_nanoseconds) / kNanosecondsPerMillisecond / static_cast<f64>(state.iterations());
      state.counters["peak_queued_frames"] = static_cast<f64>(peak_queued_frames);
    }
  }
}// namespace

auto RegisterCaptureSequence(BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  Camera const &camera,
  std::atomic_bool &failed) -> void
{
  for (auto const mode : { CaptureMode::kDiscard, CaptureMode::kSynchronousPng, CaptureMode::kQueuedPng }) {
    for (auto const trajectory : { CaptureTrajectory::kFixed, CaptureTrajectory::kOrbit }) {
      char const *base_name = "BM_CaptureQueuedPng";
      if (mode == CaptureMode::kDiscard) {
        base_name = "BM_CaptureDiscard";
      } else if (mode == CaptureMode::kSynchronousPng) {
        base_name = "BM_CaptureSynchronousPng";
      }
      auto const name = std::format("{}/{}", base_name, trajectory == CaptureTrajectory::kFixed ? "fixed" : "orbit");
      auto *registered = ::benchmark::RegisterBenchmark(
        name, [mode, trajectory, &profile, &scene_path, &camera, &failed](::benchmark::State &state) -> void {
          RunCapture(state, mode, trajectory, profile, scene_path, camera, failed);
        });
      registered->Args({ kCaptureWidth1080p, kCaptureHeight1080p })
        ->Args({ kCaptureWidth4k, kCaptureHeight4k })
        ->UseRealTime()
        ->Unit(::benchmark::kMillisecond);
    }
  }
}

}// namespace vkgsplat::benchmark
