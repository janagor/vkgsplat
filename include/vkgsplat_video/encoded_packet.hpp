#ifndef VKGSPLAT_VIDEO_ENCODED_PACKET_HPP
#define VKGSPLAT_VIDEO_ENCODED_PACKET_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vkgsplat {

// One complete H.264 access unit. Frame indices are consecutive for accepted
// pictures; timestamps use the configured frame-rate time base and may have gaps.
struct EncodedPacket
{
  std::vector<std::byte> data;
  std::uint64_t frame_index{};
  std::int64_t pts{};
  std::int64_t dts{};
  std::int64_t duration = 1;
  bool keyframe = false;
};

}// namespace vkgsplat

#endif// VKGSPLAT_VIDEO_ENCODED_PACKET_HPP
