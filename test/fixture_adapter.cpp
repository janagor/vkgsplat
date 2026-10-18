#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>

TEST_CASE("renderer adapter uses the benchmark profile", "[fixture]")
{
  auto const profile = vkgsplat::benchmark::BaselineProfile();
  auto const renderer = vkgsplat::benchmark::MakeRendererConfig(profile, "baseline-v1.ply");
  REQUIRE(renderer.ply_path == "baseline-v1.ply");
  REQUIRE_FALSE(renderer.enable_imgui);
  REQUIRE_FALSE(renderer.enable_gpu_timers);
  REQUIRE_FALSE(renderer.enable_validation);
  REQUIRE(renderer.frame_rate.mode == vkgsplat::FrameRateMode::kUncapped);
  std::array<vkgsplat::u32, 2> const mono_grid{ vkgsplat::benchmark::kMonoGridSize,
    vkgsplat::benchmark::kMonoGridSize };
  REQUIRE(renderer.lfd_grid == mono_grid);
}
