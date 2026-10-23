#ifndef VKGSPLAT_BENCHMARK_FIXTURE_PROFILE_HPP
#define VKGSPLAT_BENCHMARK_FIXTURE_PROFILE_HPP

#include <vkgsplat/camera.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <string_view>

namespace vkgsplat::benchmark {

inline constexpr std::string_view kBaselineId = "baseline-v1";
inline constexpr std::string_view kCustomId = "custom-v1";
inline constexpr std::string_view kDistributionId = "volume-cloud-v1";
inline constexpr u32 kBaselineCount = 262'144U;
inline constexpr u64 kBaselineSeed = 0x5EED2026ULL;
inline constexpr u64 kBaselinePlyHash = 0xAD124868B0D228B3ULL;
inline constexpr u32 kGeneratorVersion = 1U;
inline constexpr u32 kManifestSchemaVersion = 1U;
inline constexpr u32 kBaselineWidth = 1920U;
inline constexpr u32 kBaselineHeight = 1080U;
inline constexpr u32 kMonoGridSize = 1U;
inline constexpr f64 kBaselineFovDegrees = 45.0;
inline constexpr f64 kBaselineNearPlane = 0.1;
inline constexpr f64 kBaselineFarPlane = 100.0;
inline constexpr f64 kCameraDistance = 6.0;

struct BenchmarkSceneSpec
{
  u32 gaussian_count{ kBaselineCount };
  u64 seed{ kBaselineSeed };
  u32 generator_version{ kGeneratorVersion };
};

struct BenchmarkProfile
{
  std::string_view id{ kBaselineId };
  BenchmarkSceneSpec scene{};
  u32 width{ kBaselineWidth };
  u32 height{ kBaselineHeight };
  CameraConfig camera{
    .position = { 0.0, 0.0, kCameraDistance },
    .target = { 0.0, 0.0, 0.0 },
    .up = { 0.0, 1.0, 0.0 },
    .orientation = std::nullopt,
    .fov_degrees = kBaselineFovDegrees,
    .aspect_ratio = static_cast<f64>(kBaselineWidth) / static_cast<f64>(kBaselineHeight),
    .near_plane = kBaselineNearPlane,
    .far_plane = kBaselineFarPlane,
  };
  u32 sh_degree{ gs::kShDegree };
  bool enable_imgui{ false };
  bool enable_gpu_timers{ false };
  bool enable_validation{ false };
  FrameRateMode frame_rate_mode{ FrameRateMode::kUncapped };
};

[[nodiscard]] auto BaselineProfile() -> BenchmarkProfile;
[[nodiscard]] auto IsCanonical(BenchmarkSceneSpec const &spec) -> bool;

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_FIXTURE_PROFILE_HPP
