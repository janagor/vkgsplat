#ifndef VKGSPLAT_CAPTURE_VIDEO_CONFIG_HPP
#define VKGSPLAT_CAPTURE_VIDEO_CONFIG_HPP

#include <filesystem>

#include <vkgsplat_capture/frame_sequence.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::capture {

enum class VideoContainer { kH264, kMp4 };
enum class RateControlMode { kDefault, kConstantQp, kCbr, kVbr };

struct RateControlConfig
{
  RateControlMode mode = RateControlMode::kDefault;
  u64 target_bitrate = 8'000'000U;
  u64 max_bitrate = 12'000'000U;
  u32 constant_qp = 26U;
};

struct GopConfig
{
  u32 idr_interval = 60U;
  bool enable_p_frames = true;
};

struct VideoConfig
{
  std::filesystem::path output_path;
  VideoContainer container = VideoContainer::kMp4;
  u32 width = 1920U;
  u32 height = 1080U;
  FrameRate frame_rate{};
  RateControlConfig rate_control{};
  GopConfig gop{};
};

}// namespace vkgsplat::capture

#endif// VKGSPLAT_CAPTURE_VIDEO_CONFIG_HPP
