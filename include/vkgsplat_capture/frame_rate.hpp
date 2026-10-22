#ifndef VKGSPLAT_CAPTURE_FRAME_RATE_HPP
#define VKGSPLAT_CAPTURE_FRAME_RATE_HPP

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::capture {

struct FrameRate
{
  static constexpr u32 kDefaultFramesPerSecond = 60U;
  u32 numerator = kDefaultFramesPerSecond;
  u32 denominator = 1U;
};

}// namespace vkgsplat::capture

#endif// VKGSPLAT_CAPTURE_FRAME_RATE_HPP
