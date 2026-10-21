#ifndef VKGSPLAT_BACKEND_VULKAN_VIDEO_H264_GOP_HPP
#define VKGSPLAT_BACKEND_VULKAN_VIDEO_H264_GOP_HPP

#include <cstdint>
#include <expected>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan::video {

enum class PictureType { kIdr, kP };

struct GopPicture
{
  PictureType type{};
  std::uint64_t frame_index{};
  std::uint32_t frame_num{};
  std::uint32_t poc_lsb{};
  std::uint32_t remaining_frames{};
};

// Computes an IDR/P display-order schedule without B-frame reordering.
class H264Gop
{
public:
  [[nodiscard]] static auto Create(std::uint32_t idr_interval, bool enable_p_frames)
    -> std::expected<H264Gop, Error>;
  [[nodiscard]] auto picture(std::uint64_t frame_index) const noexcept -> GopPicture;

private:
  H264Gop(std::uint32_t idr_interval, bool enable_p_frames) noexcept
    : idr_interval_{ idr_interval }, enable_p_frames_{ enable_p_frames } {}

  std::uint32_t idr_interval_{};
  bool enable_p_frames_ = false;
};

}// namespace vkgsplat::vulkan::video

#endif// VKGSPLAT_BACKEND_VULKAN_VIDEO_H264_GOP_HPP
