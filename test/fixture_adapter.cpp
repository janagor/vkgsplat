// NOLINTBEGIN(readability-function-cognitive-complexity)
#include "benchmark/batch_accounting.hpp"
#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat/renderer.hpp>
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
  REQUIRE(renderer.submission_policy == vkgsplat::FrameSubmissionPolicy::kLossless);
  REQUIRE(renderer.frame_rate.mode == vkgsplat::FrameRateMode::kUncapped);
  std::array<vkgsplat::u32, 2> const mono_grid{ vkgsplat::benchmark::kMonoGridSize,
    vkgsplat::benchmark::kMonoGridSize };
  REQUIRE(renderer.lfd_grid == mono_grid);
}

TEST_CASE("GPU profiling adapter enables timestamp sample collection", "[fixture]")
{
  auto const profile = vkgsplat::benchmark::BaselineProfile();
  auto const renderer = vkgsplat::benchmark::MakeGpuProfilingRendererConfig(profile, "baseline-v1.ply");
  REQUIRE(renderer.enable_gpu_timers);
  REQUIRE(renderer.capture_gpu_timing_samples);
  REQUIRE(renderer.submission_policy == vkgsplat::FrameSubmissionPolicy::kLossless);
}

TEST_CASE("benchmark accounting rejects missing and recreated frames", "[fixture]")
{
  constexpr vkgsplat::u64 kRequested = 64U;
  constexpr vkgsplat::u64 kBefore = 120U;
  REQUIRE(vkgsplat::benchmark::ValidateBatchAccounting(kRequested, kBefore, kBefore + kRequested, 0U, 0U));
  REQUIRE_FALSE(vkgsplat::benchmark::ValidateBatchAccounting(kRequested, kBefore, kBefore + kRequested - 1U, 0U, 0U));
  REQUIRE_FALSE(vkgsplat::benchmark::ValidateBatchAccounting(kRequested, kBefore, kBefore + kRequested, 0U, 1U));
  REQUIRE_FALSE(vkgsplat::benchmark::ValidateBatchAccounting(kRequested, kBefore, kBefore - 1U, 0U, 0U));
}
// NOLINTEND(readability-function-cognitive-complexity)
