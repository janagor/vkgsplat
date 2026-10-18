#include "benchmark/fixture/renderer_adapter.hpp"
#include "benchmark/fixture/profile.hpp"

#include <vkgsplat/renderer.hpp>

#include <filesystem>

namespace vkgsplat::benchmark {

auto MakeRendererConfig(BenchmarkProfile const &profile, std::filesystem::path const &scene_path) -> RendererConfig
{
  return RendererConfig{
    .ply_path = scene_path.string(),
    .enable_validation = profile.enable_validation,
    .enable_imgui = profile.enable_imgui,
    .enable_gpu_timers = profile.enable_gpu_timers,
    .frame_rate = { .mode = profile.frame_rate_mode },
    .lfd_grid = { kMonoGridSize, kMonoGridSize },
    .lfd_view_order = {},
  };
}

}// namespace vkgsplat::benchmark
