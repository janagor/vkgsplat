#ifndef VKGSPLAT_IO_VIDEO_VIDEO_SINK_HPP
#define VKGSPLAT_IO_VIDEO_VIDEO_SINK_HPP

#include <vkgsplat_video/encoded_packet.hpp>

#include <expected>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

class VideoSink
{
public:
  virtual ~VideoSink() = default;
  virtual auto Write(EncodedPacket const &packet) -> std::expected<void, Error> = 0;
  virtual auto Finish() -> std::expected<void, Error> = 0;
};

}// namespace vkgsplat

#endif// VKGSPLAT_IO_VIDEO_VIDEO_SINK_HPP
