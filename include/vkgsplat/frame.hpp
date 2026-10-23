#ifndef VKGSPLAT_FRAME_HPP
#define VKGSPLAT_FRAME_HPP

#include <cstdint>
#include <vector>

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

/** Owned, tightly packed RGBA8 pixels in top-to-bottom row order. */
struct RgbaFrame
{
  u32 width{};
  u32 height{};
  std::vector<std::uint8_t> pixels;
};

}// namespace vkgsplat

#endif// VKGSPLAT_FRAME_HPP
