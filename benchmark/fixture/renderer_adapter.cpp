#include "benchmark/fixture/renderer_adapter.hpp"
#include "benchmark/fixture/profile.hpp"

#include <vkgsplat/renderer.hpp>

#include <filesystem>

namespace vkgsplat::benchmark {

auto MakeRendererConfig(BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  RasterizationMode mode) -> RendererConfig
{
  return RendererConfig{
    .ply_path = scene_path.string(),
    .enable_validation = profile.enable_validation,
    .enable_imgui = profile.enable_imgui,
    .enable_gpu_timers = profile.enable_gpu_timers,
    .submission_policy = FrameSubmissionPolicy::kLossless,
    .rasterization_mode = mode,
    .tiled_intersection_budget_bytes = profile.tiled_intersection_budget_bytes,
    .frame_rate = { .mode = profile.frame_rate_mode },
    .lfd_grid = { kMonoGridSize, kMonoGridSize },
    .lfd_view_order = {},
  };
}

auto MakeGpuProfilingRendererConfig(BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  RasterizationMode mode) -> RendererConfig
{
  auto config = MakeRendererConfig(profile, scene_path, mode);
  config.enable_gpu_timers = true;
  config.capture_gpu_timing_samples = true;
  config.rasterization_mode = mode;
  return config;
}

}// namespace vkgsplat::benchmark
