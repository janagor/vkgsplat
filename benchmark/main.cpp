#include "benchmark/capture_sequence.hpp"
#include "benchmark/fixture/manifest.hpp"
#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/ply_writer.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"
#include "benchmark/fixture/scene.hpp"
#include "benchmark/gpu_pass_timings.hpp"
#include "benchmark/render_throughput.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_headless/headless_platform.hpp>

// clang-tidy does not associate Google Benchmark's declarations with this umbrella header.
// NOLINTBEGIN(misc-include-cleaner)
#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <print>
#include <string>

namespace {

constexpr vkgsplat::u32 kOverlapGaussianCount = 4'096U;
constexpr vkgsplat::f32 kLowOverlapLogScale = -5.5F;
constexpr vkgsplat::f32 kHighOverlapLogScale = -2.5F;
constexpr vkgsplat::f32 kHighOverlapPositionScale = 0.15F;

[[nodiscard]] auto WriteOverlapScene(std::filesystem::path const &path, bool high_overlap) -> bool
{
  auto scene = vkgsplat::benchmark::GenerateScene({ .gaussian_count = kOverlapGaussianCount,
    .seed = vkgsplat::benchmark::kBaselineSeed,
    .generator_version = vkgsplat::benchmark::kGeneratorVersion });
  if (!scene) { std::println(stderr, "{}", scene.error().message()); return false; }
  for (auto &geometry : scene->geometries) {
    geometry.scale.fill(high_overlap ? kHighOverlapLogScale : kLowOverlapLogScale);
    if (high_overlap) {
      std::ranges::transform(geometry.position, geometry.position.begin(),
        [](vkgsplat::f32 position) -> vkgsplat::f32 { return position * kHighOverlapPositionScale; });
    }
  }
  auto written = vkgsplat::benchmark::WritePly(path, *scene);
  if (!written) { std::println(stderr, "{}", written.error().message()); }
  return written.has_value();
}

auto RunBenchmark(int argc, char **argv) -> int
{
  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) { return 1; }

  auto const scene_path = std::filesystem::path{ VKGSPLAT_BENCHMARK_SCENE };
  auto verified = vkgsplat::benchmark::VerifyFixture(scene_path);
  if (!verified || !verified->canonical) {
    std::println(stderr,
      "Benchmark fixture verification failed: {}",
      verified ? "fixture is not the canonical baseline" : verified.error().message());
    return 1;
  }

  auto const profile = vkgsplat::benchmark::BaselineProfile();
  auto platform = vkgsplat::HeadlessPlatform::create({ .width = profile.width, .height = profile.height });
  if (!platform) {
    std::println(stderr, "{}", platform.error().message());
    return 1;
  }
  auto const camera = vkgsplat::Camera{ profile.camera };
  {
    auto renderer = vkgsplat::Renderer::create(vkgsplat::benchmark::MakeRendererConfig(profile, scene_path), *platform);
    if (!renderer) {
      std::println(stderr, "{}", renderer.error().message());
      return 1;
    }
    auto const device = renderer->device_info();
    ::benchmark::AddCustomContext("gpu_name", device.name);
    ::benchmark::AddCustomContext("gpu_vendor_id", std::to_string(device.vendor_id));
    ::benchmark::AddCustomContext("gpu_device_id", std::to_string(device.device_id));
    ::benchmark::AddCustomContext("gpu_driver_version", std::to_string(device.driver_version));
    ::benchmark::AddCustomContext("vulkan_api_version", std::to_string(device.vulkan_api_version));
    ::benchmark::AddCustomContext("git_commit", VKGSPLAT_BENCHMARK_GIT_SHA);
    ::benchmark::AddCustomContext("git_dirty", VKGSPLAT_BENCHMARK_GIT_DIRTY);
    ::benchmark::AddCustomContext("build_type", VKGSPLAT_BENCHMARK_BUILD_TYPE);
    ::benchmark::AddCustomContext("ipo_enabled", VKGSPLAT_BENCHMARK_IPO);
    ::benchmark::AddCustomContext("scene_profile", std::string{ profile.id });
    ::benchmark::AddCustomContext("scene_checksum_fnv1a64", std::format("{:016x}", verified->hash));
    ::benchmark::AddCustomContext("scene_seed", std::to_string(profile.scene.seed));
    ::benchmark::AddCustomContext("gaussian_count", std::to_string(profile.scene.gaussian_count));
    ::benchmark::AddCustomContext("resolution", std::format("{}x{}", profile.width, profile.height));
    ::benchmark::AddCustomContext("fov_degrees", std::to_string(profile.camera.fov_degrees));
    ::benchmark::AddCustomContext("submission_policy", "lossless");
    ::benchmark::AddCustomContext("render_mode", "mono");
    ::benchmark::AddCustomContext("warmup_frames", std::to_string(vkgsplat::benchmark::kWarmupFrames));
    ::benchmark::AddCustomContext("frame_pacing", "uncapped");
    ::benchmark::AddCustomContext("vulkan_validation", "disabled");

    ::benchmark::AddCustomContext("gpu_timing_enabled", "throughput=false;gpu_pass_timings=true");
    ::benchmark::AddCustomContext("gpu_timing_schema_version", std::to_string(vkgsplat::kGpuTimingSchemaVersion));
    ::benchmark::AddCustomContext("gpu_timing_scope", "gs_pipeline");
    ::benchmark::AddCustomContext("gpu_timestamp_period_ns", std::to_string(renderer->gpu_timestamp_period_ns()));
    ::benchmark::AddCustomContext("gpu_timestamp_valid_bits", std::to_string(renderer->gpu_timestamp_valid_bits()));
  }

  std::atomic_bool failed{ false };
  vkgsplat::benchmark::RegisterRenderThroughput(*platform, profile, scene_path, camera, failed);
  vkgsplat::benchmark::RegisterGpuPassTimings(*platform, profile, scene_path, camera, failed);
  constexpr vkgsplat::u32 kHdWidth = 1280U;
  constexpr vkgsplat::u32 kHdHeight = 720U;
  constexpr vkgsplat::u32 kUhdWidth = 3840U;
  constexpr vkgsplat::u32 kUhdHeight = 2160U;
  auto hd_profile = profile;
  hd_profile.width = kHdWidth;
  hd_profile.height = kHdHeight;
  hd_profile.camera.aspect_ratio = static_cast<vkgsplat::f64>(kHdWidth) / kHdHeight;
  auto hd_platform = vkgsplat::HeadlessPlatform::create({ .width = kHdWidth, .height = kHdHeight });
  if (!hd_platform) { std::println(stderr, "{}", hd_platform.error().message()); return 1; }
  auto const hd_camera = vkgsplat::Camera{ hd_profile.camera };
  vkgsplat::benchmark::RegisterRenderThroughput(*hd_platform, hd_profile, scene_path, hd_camera, failed);
  vkgsplat::benchmark::RegisterGpuPassTimings(*hd_platform, hd_profile, scene_path, hd_camera, failed);
  auto uhd_profile = profile;
  uhd_profile.width = kUhdWidth;
  uhd_profile.height = kUhdHeight;
  uhd_profile.camera.aspect_ratio = static_cast<vkgsplat::f64>(kUhdWidth) / kUhdHeight;
  uhd_profile.tiled_intersection_budget_bytes = vkgsplat::benchmark::kUhdTiledBudgetBytes;
  auto uhd_platform = vkgsplat::HeadlessPlatform::create({ .width = kUhdWidth, .height = kUhdHeight });
  if (!uhd_platform) { std::println(stderr, "{}", uhd_platform.error().message()); return 1; }
  auto const uhd_camera = vkgsplat::Camera{ uhd_profile.camera };
  vkgsplat::benchmark::RegisterRenderThroughput(*uhd_platform, uhd_profile, scene_path, uhd_camera, failed);
  vkgsplat::benchmark::RegisterGpuPassTimings(*uhd_platform, uhd_profile, scene_path, uhd_camera, failed);
  auto low_profile = profile;
  low_profile.id = "low-overlap-v1";
  low_profile.scene.gaussian_count = kOverlapGaussianCount;
  auto high_profile = low_profile;
  high_profile.id = "high-overlap-v1";
  auto const low_scene_path = scene_path.parent_path() / "low-overlap-v1.ply";
  auto const high_scene_path = scene_path.parent_path() / "high-overlap-v1.ply";
  if (!WriteOverlapScene(low_scene_path, false) || !WriteOverlapScene(high_scene_path, true)) { return 1; }
  auto const low_hash = vkgsplat::benchmark::FingerprintFile(low_scene_path);
  auto const high_hash = vkgsplat::benchmark::FingerprintFile(high_scene_path);
  if (!low_hash || !high_hash) { return 1; }
  ::benchmark::AddCustomContext("low_overlap_checksum_fnv1a64", std::format("{:016x}", *low_hash));
  ::benchmark::AddCustomContext("high_overlap_checksum_fnv1a64", std::format("{:016x}", *high_hash));
  vkgsplat::benchmark::RegisterRenderThroughput(*platform, low_profile, low_scene_path, camera, failed);
  vkgsplat::benchmark::RegisterGpuPassTimings(*platform, low_profile, low_scene_path, camera, failed);
  vkgsplat::benchmark::RegisterRenderThroughput(*platform, high_profile, high_scene_path, camera, failed);
  vkgsplat::benchmark::RegisterGpuPassTimings(*platform, high_profile, high_scene_path, camera, failed);
  vkgsplat::benchmark::RegisterCaptureSequence(profile, scene_path, camera, failed);
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return failed.load() ? EXIT_FAILURE : EXIT_SUCCESS;
}
// NOLINTEND(misc-include-cleaner)

}// namespace

auto main(int argc, char **argv) -> int
{
  try {
    return RunBenchmark(argc, argv);
  } catch (std::exception const &error) {
    try {
      std::println(stderr, "Benchmark failed: {}", error.what());
    } catch (...) {
      return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
  } catch (...) {
    return EXIT_FAILURE;
  }
}
