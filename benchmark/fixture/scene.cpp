#include "benchmark/fixture/scene.hpp"
#include "benchmark/fixture/profile.hpp"

#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <expected>
#include <system_error>

namespace vkgsplat::benchmark {
namespace {

  inline constexpr u64 kMixIncrement = 0x9E3779B97F4A7C15ULL;
  inline constexpr u64 kMixMultiplierA = 0xBF58476D1CE4E5B9ULL;
  inline constexpr u64 kMixMultiplierB = 0x94D049BB133111EBULL;
  inline constexpr u32 kMixShiftA = 30U;
  inline constexpr u32 kMixShiftB = 27U;
  inline constexpr u32 kMixShiftFinal = 31U;
  inline constexpr u32 kSampleBits = 16U;
  inline constexpr u64 kSampleMask = (u64{ 1 } << kSampleBits) - 1U;
  inline constexpr u32 kSampleRange = 1U << kSampleBits;
  inline constexpr u32 kPositionQuantization = 4096U;
  inline constexpr u32 kDcQuantization = kSampleRange / 2U;
  inline constexpr i32 kDcCenter = static_cast<i32>(kSampleRange / 2U);
  inline constexpr u32 kRestValueCount = 31U;
  inline constexpr i32 kRestCenter = 15;
  inline constexpr u32 kRestQuantization = 1024U;
  inline constexpr u32 kPositionAttribute = 0U;
  inline constexpr u32 kScaleAttribute = 3U;
  inline constexpr u32 kRotationAttribute = 6U;
  inline constexpr u32 kOpacityAttribute = 7U;
  inline constexpr u32 kDcAttribute = 8U;
  inline constexpr u32 kRestAttribute = 11U;
  inline constexpr std::array<i32, 3> kPositionExtentUnits{ 22'528, 12'288, 8'192 };
  inline constexpr std::array<i32, 3> kPositionMinimumUnits{ -11'264, -6'144, -4'096 };
  inline constexpr std::array<f32, 4> kLogScales{ -4.25F, -4.0F, -3.75F, -3.5F };
  inline constexpr std::array<f32, 4> kOpacityLogits{ 0.0F, 0.5F, 1.0F, 1.5F };
  inline constexpr std::array<std::array<f32, 4>, 8> kRotations{
    std::array<f32, 4>{ 1.0F, 0.0F, 0.0F, 0.0F },
    { 0.0F, 1.0F, 0.0F, 0.0F },
    { 0.0F, 0.0F, 1.0F, 0.0F },
    { 0.0F, 0.0F, 0.0F, 1.0F },
    { 0.5F, 0.5F, 0.5F, 0.5F },
    { 0.5F, 0.5F, -0.5F, -0.5F },
    { 0.5F, -0.5F, 0.5F, -0.5F },
    { 0.5F, -0.5F, -0.5F, 0.5F },
  };

  [[nodiscard]] auto Mix(u64 value) -> u64
  {
    value += kMixIncrement;
    value = (value ^ (value >> kMixShiftA)) * kMixMultiplierA;
    value = (value ^ (value >> kMixShiftB)) * kMixMultiplierB;
    return value ^ (value >> kMixShiftFinal);
  }

  [[nodiscard]] auto Sample(u64 seed, u32 index, u32 attribute) -> u64
  { return Mix(seed ^ Mix(static_cast<u64>(index)) ^ Mix(static_cast<u64>(attribute))); }

}// namespace

auto GenerateScene(BenchmarkSceneSpec const &spec) -> std::expected<SplatCpuData, Error>
{
  if (spec.gaussian_count == 0U || spec.generator_version != kGeneratorVersion) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "invalid scene count or generator version") };
  }

  SplatCpuData scene{};
  scene.geometries.resize(spec.gaussian_count);
  scene.appearances.resize(spec.gaussian_count);
  for (u32 index = 0; index < spec.gaussian_count; ++index) {
    auto &geometry = scene.geometries.at(index);
    auto &appearance = scene.appearances.at(index);
    for (u32 axis = 0; axis < geometry.position.size(); ++axis) {
      auto const position_sample = static_cast<i32>(Sample(spec.seed, index, kPositionAttribute + axis) & kSampleMask);
      auto const position_units = kPositionMinimumUnits.at(axis)
                                  + (position_sample * kPositionExtentUnits.at(axis) / static_cast<i32>(kSampleRange));
      geometry.position.at(axis) = static_cast<f32>(position_units) / static_cast<f32>(kPositionQuantization);
      geometry.scale.at(axis) = kLogScales.at(Sample(spec.seed, index, kScaleAttribute + axis) % kLogScales.size());
    }
    geometry.rotation = kRotations.at(Sample(spec.seed, index, kRotationAttribute) % kRotations.size());
    geometry.opacity = kOpacityLogits.at(Sample(spec.seed, index, kOpacityAttribute) % kOpacityLogits.size());
    for (u32 channel = 0; channel < appearance.f_dc.size(); ++channel) {
      auto const dc_sample = static_cast<i32>(Sample(spec.seed, index, kDcAttribute + channel) & kSampleMask);
      appearance.f_dc.at(channel) = static_cast<f32>(dc_sample - kDcCenter) / static_cast<f32>(kDcQuantization);
    }
    for (u32 coefficient = 0; coefficient < appearance.f_rest.size(); ++coefficient) {
      appearance.f_rest.at(coefficient) =
        static_cast<f32>(
          static_cast<i32>(Sample(spec.seed, index, kRestAttribute + coefficient) % kRestValueCount) - kRestCenter)
        / static_cast<f32>(kRestQuantization);
    }
  }
  return scene;
}

}// namespace vkgsplat::benchmark
