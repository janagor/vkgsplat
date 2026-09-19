#ifndef VKGSPLAT_FRAME_RATE_HPP
#define VKGSPLAT_FRAME_RATE_HPP

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

/** Present pacing policy used by a Renderer. */
enum class FrameRateMode : u8 {
  kUncapped = 0,
  kFixed = 1,
  kDisplay = 2,
  kAdaptive = 3,
};

/** Optional present pacing requested by the application configuration. */
struct FrameRateConfig
{
  FrameRateMode mode = FrameRateMode::kUncapped;
  u32 fixed_fps = 0;// used when mode == kFixed

  [[nodiscard]] constexpr auto IsPacingRequested() const noexcept -> bool { return mode != FrameRateMode::kUncapped; }
};

}// namespace vkgsplat

#endif// VKGSPLAT_FRAME_RATE_HPP
