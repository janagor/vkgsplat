#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_capture/frame_sequence.hpp>
#include <vkgsplat_capture/screenshot.hpp>
#include <vkgsplat_headless/offscreen_platform.hpp>

#include <benchmark/benchmark.h>// NOLINT(misc-include-cleaner)

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
#include <system_error>
#include <string>
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

enum class CaptureMode { kDiscard, kSynchronousPng, kQueuedPng };
enum class CaptureTrajectory { kFixed, kOrbit };

[[nodiscard]] auto MakeOutputPath() -> std::filesystem::path
{
  static std::atomic_uint64_t next_path{ 0U };
  auto const tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path()
    / std::format("vkgsplat-capture-benchmark-{}-{}", tick, next_path.fetch_add(1U));
}

[[nodiscard]] auto MakeOrbitCamera(BenchmarkProfile const &profile, u64 index) -> Camera
{
  auto config = profile.camera;
  auto const dx = config.position.x - config.target.x;
  auto const dz = config.position.z - config.target.z;
  auto const angle = static_cast<f64>(index) / static_cast<f64>(kCaptureFrames) * kFullCircleRadians;
  config.position.x = config.target.x + dx * std::cos(angle) + dz * std::sin(angle);
  config.position.z = config.target.z - dx * std::sin(angle) + dz * std::cos(angle);
  return Camera{ config };
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
    std::expected<void, Error> result;
    if (mode == CaptureMode::kQueuedPng) {
      auto sequence = capture::FrameSequence::create(*renderer, { .output_directory = output });
      if (!sequence) { result = std::unexpected{ sequence.error() }; }
      for (u64 index = 0U; result && index < kCaptureFrames; ++index) {
        auto const &capture_camera = trajectory == CaptureTrajectory::kFixed ? camera : orbit_cameras.at(index);
        auto recorded = sequence->record_frame(capture_camera);
        if (!recorded) { result = std::unexpected{ recorded.error() }; }
      }
      if (result) {
        auto finished = sequence->finish();
        if (!finished || finished->frame_count != kCaptureFrames) {
          result = std::unexpected{ finished ? MakeError(std::errc::io_error, "Incomplete sequence") : finished.error() };
        } else {
          queue_wait_nanoseconds += finished->queue_wait_nanoseconds;
          peak_queued_frames = std::max(peak_queued_frames, finished->peak_queued_frames);
        }
      }
    } else {
      for (u64 index = 0U; index < kCaptureFrames; ++index) {
        auto const &capture_camera = trajectory == CaptureTrajectory::kFixed ? camera : orbit_cameras.at(index);
        auto frame = renderer->render_frame(capture_camera);
        if (!frame) { result = std::unexpected{ frame.error() }; break; }
        if (mode == CaptureMode::kSynchronousPng) {
          auto written = capture::write_png(*frame, (output / std::format("frame_{:06}.png", index)).string());
          if (!written) { result = std::unexpected{ written.error() }; break; }
        }
      }
    }
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
    state.counters["queue_wait_ms_per_batch"] = static_cast<f64>(queue_wait_nanoseconds)
      / kNanosecondsPerMillisecond / static_cast<f64>(state.iterations());
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
      auto const base_name = mode == CaptureMode::kDiscard ? "BM_CaptureDiscard"
        : mode == CaptureMode::kSynchronousPng ? "BM_CaptureSynchronousPng" : "BM_CaptureQueuedPng";
      auto const name = std::format("{}/{}", base_name,
        trajectory == CaptureTrajectory::kFixed ? "fixed" : "orbit");
      auto *registered = ::benchmark::RegisterBenchmark(name,
        [mode, trajectory, &profile, &scene_path, &camera, &failed](::benchmark::State &state) -> void {
          RunCapture(state, mode, trajectory, profile, scene_path, camera, failed);
        });
      registered->Args({ kCaptureWidth1080p, kCaptureHeight1080p })
        ->Args({ kCaptureWidth4k, kCaptureHeight4k })
        ->UseRealTime()->Unit(::benchmark::kMillisecond);
    }
  }
}

}// namespace vkgsplat::benchmark
