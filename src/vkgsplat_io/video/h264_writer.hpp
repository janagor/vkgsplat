#ifndef VKGSPLAT_IO_VIDEO_H264_WRITER_HPP
#define VKGSPLAT_IO_VIDEO_H264_WRITER_HPP

#include <cstddef>
#include <expected>
#include <fstream>
#include <span>
#include <string_view>
#include <utility>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

// Writes already encoded Annex B NAL units in display order. The implementation
// checks framing and NAL types; it does not validate H.264 syntax or decoding.
class H264Writer
{
public:
  [[nodiscard]] static auto Open(std::string_view path, std::span<std::byte const> parameter_sets)
    -> std::expected<H264Writer, Error>;

  auto WriteFrame(std::span<std::byte const> picture) -> std::expected<void, Error>;
  auto Finish() -> std::expected<void, Error>;

private:
  explicit H264Writer(std::ofstream stream) : stream_{ std::move(stream) } {}

  std::ofstream stream_;
  bool finished_ = false;
  bool seen_idr_ = false;
};

}// namespace vkgsplat

#endif// VKGSPLAT_IO_VIDEO_H264_WRITER_HPP
