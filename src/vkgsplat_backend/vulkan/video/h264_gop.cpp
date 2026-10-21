#include "h264_gop.hpp"

#include <system_error>

namespace vkgsplat::vulkan::video {
namespace {

  // VideoSession currently writes log2_max_frame_num_minus4 and
  // log2_max_pic_order_cnt_lsb_minus4 as zero in its SPS.
  constexpr std::uint32_t kFrameNumberBits = 4U;
  constexpr std::uint32_t kFrameNumberModulus = 1U << kFrameNumberBits;
  constexpr std::uint32_t kPocModulus = 1U << kFrameNumberBits;
  constexpr std::uint32_t kFieldCountPerFrame = 2U;

}// namespace

auto H264Gop::Create(std::uint32_t idr_interval, bool enable_p_frames)
  -> std::expected<H264Gop, Error>
{
  if (idr_interval == 0U) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "IDR interval must be positive"));
  }
  return H264Gop{ idr_interval, enable_p_frames };
}

auto H264Gop::picture(std::uint64_t frame_index) const noexcept -> GopPicture
{
  auto const position = static_cast<std::uint32_t>(frame_index % idr_interval_);
  auto const idr = !enable_p_frames_ || position == 0U;
  return GopPicture{
    .type = idr ? PictureType::kIdr : PictureType::kP,
    .frame_index = frame_index,
    .frame_num = idr ? 0U : position % kFrameNumberModulus,
    .poc_lsb = idr ? 0U : kFieldCountPerFrame * (position % (kPocModulus / kFieldCountPerFrame)),
    .remaining_frames = enable_p_frames_ ? idr_interval_ - position - 1U : 0U,
  };
}

}// namespace vkgsplat::vulkan::video
