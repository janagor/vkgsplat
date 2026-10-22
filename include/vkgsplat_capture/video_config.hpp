#ifndef VKGSPLAT_CAPTURE_VIDEO_CONFIG_HPP
#define VKGSPLAT_CAPTURE_VIDEO_CONFIG_HPP

#include <cstdint>
#include <filesystem>

#include <vkgsplat_capture/frame_rate.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::capture {

inline constexpr u64 kDefaultVideoTargetBitrate = 8'000'000U;
inline constexpr u64 kDefaultVideoMaximumBitrate = 12'000'000U;
inline constexpr u32 kDefaultVideoConstantQp = 26U;
inline constexpr u32 kDefaultVideoWidth = 1280U;
inline constexpr u32 kDefaultVideoHeight = 720U;

enum class VideoContainer : std::uint8_t { kH264, kMp4 };
enum class RateControlMode : std::uint8_t { kDefault, kConstantQp, kCbr, kVbr };

struct RateControlConfig
{
  RateControlMode mode = RateControlMode::kDefault;
  u64 target_bitrate = kDefaultVideoTargetBitrate;
  u64 max_bitrate = kDefaultVideoMaximumBitrate;
  u32 constant_qp = kDefaultVideoConstantQp;
};

struct GopConfig
{
  u32 idr_interval = 1U;
  bool enable_p_frames = false;
};

struct VideoConfig
{
  std::filesystem::path output_path;
  VideoContainer container = VideoContainer::kH264;
  u32 width = kDefaultVideoWidth;
  u32 height = kDefaultVideoHeight;
  FrameRate frame_rate{};
  RateControlConfig rate_control{};
  GopConfig gop{};
};

}// namespace vkgsplat::capture

#endif// VKGSPLAT_CAPTURE_VIDEO_CONFIG_HPP
