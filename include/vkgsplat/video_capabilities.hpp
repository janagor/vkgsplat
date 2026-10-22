#ifndef VKGSPLAT_VIDEO_CAPABILITIES_HPP
#define VKGSPLAT_VIDEO_CAPABILITIES_HPP

#include <cstdint>
#include <string>

namespace vkgsplat {

enum class VideoEncodeSupport : std::uint8_t { kDisabled, kOptional, kRequired };

/** Summary of H.264 support enabled on this driver's logical device. */
struct VideoEncodeCapabilities
{
  bool h264_enabled = false;
  /// An encode-source image permits transfer destinations or storage writes.
  bool capture_conversion_supported = false;
  bool reference_pictures_supported = false;
  std::uint32_t max_width = 0;
  std::uint32_t max_height = 0;
  std::string diagnostic;
};

}// namespace vkgsplat

#endif// VKGSPLAT_VIDEO_CAPABILITIES_HPP
