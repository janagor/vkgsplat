#include "benchmark/fixture/manifest.hpp"
#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"
#include "benchmark/render_throughput.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_headless/headless_platform.hpp>

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <print>
#include <string>

namespace {

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
  auto renderer = vkgsplat::Renderer::create(vkgsplat::benchmark::MakeRendererConfig(profile, scene_path), *platform);
  if (!renderer) {
    std::println(stderr, "{}", renderer.error().message());
    return 1;
  }
  auto const camera = vkgsplat::Camera{ profile.camera };
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

  std::atomic_bool failed{ false };
  vkgsplat::benchmark::RegisterRenderThroughput(*renderer, camera, failed);
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return failed.load() ? EXIT_FAILURE : EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  try {
    return RunBenchmark(argc, argv);
  } catch (std::exception const &error) {
    std::println(stderr, "Benchmark failed: {}", error.what());
    return 1;
  }
}
