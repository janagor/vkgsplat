#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_TIMESTAMP_MATH_HPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_TIMESTAMP_MATH_HPP

#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>
#include <optional>

namespace vkgsplat {

inline constexpr double kNanosecondsToMilliseconds = 1.0e-6;
inline constexpr size_t kTimestampPairValueCount = 4U;

[[nodiscard]] inline auto TimestampDeltaMs(u64 begin_ticks, u64 end_ticks, u32 valid_bits, double period_ns)
  -> std::optional<double>
{
  constexpr u32 kTimestampWidth = 64U;
  if (valid_bits == 0U || valid_bits > kTimestampWidth || period_ns <= 0.0) { return std::nullopt; }
  u64 const mask = valid_bits == kTimestampWidth ? ~u64{ 0U } : (u64{ 1U } << valid_bits) - 1U;
  u64 const delta = (end_ticks - begin_ticks) & mask;
  return static_cast<double>(delta) * period_ns * kNanosecondsToMilliseconds;
}

[[nodiscard]] inline auto AvailableTimestampPairMs(std::array<u64, kTimestampPairValueCount> const &values,
  u32 valid_bits,
  double period_ns) -> std::optional<double>
{
  constexpr size_t kBeginTickIndex = 0U;
  constexpr size_t kBeginAvailabilityIndex = 1U;
  constexpr size_t kEndTickIndex = 2U;
  constexpr size_t kEndAvailabilityIndex = 3U;
  if (values.at(kBeginAvailabilityIndex) == 0U || values.at(kEndAvailabilityIndex) == 0U) { return std::nullopt; }
  return TimestampDeltaMs(values.at(kBeginTickIndex), values.at(kEndTickIndex), valid_bits, period_ns);
}

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_TIMESTAMP_MATH_HPP
