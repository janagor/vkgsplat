#ifndef VKGSPLAT_IO_VIDEO_H264_WRITER_HPP
#define VKGSPLAT_IO_VIDEO_H264_WRITER_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <utility>

#include <vkgsplat_io/vkgsplat_io_export.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

// Writes already encoded Annex B NAL units in display order. The implementation
// checks framing and NAL types; it does not validate H.264 syntax or decoding.
class VKGSPLAT_IO_EXPORT H264Writer
{
public:
  [[nodiscard]] static auto Open(std::string_view path, std::span<std::byte const> parameter_sets)
    -> std::expected<H264Writer, Error>;

  auto WriteFrame(std::span<std::byte const> picture) -> std::expected<void, Error>;
  auto Finish() -> std::expected<void, Error>;
  ~H264Writer();
  H264Writer(H264Writer &&other) noexcept;
  auto operator=(H264Writer &&other) noexcept -> H264Writer &;
  H264Writer(H264Writer const &) = delete;
  auto operator=(H264Writer const &) -> H264Writer & = delete;

private:
  H264Writer(std::ofstream stream, std::filesystem::path path, std::filesystem::path temporary_path)
    : stream_{ std::move(stream) }, path_{ std::move(path) }, temporary_path_{ std::move(temporary_path) }
  {}

  std::ofstream stream_;
  std::filesystem::path path_;
  std::filesystem::path temporary_path_;
  bool finished_ = false;
  bool seen_idr_ = false;
};

}// namespace vkgsplat

#endif// VKGSPLAT_IO_VIDEO_H264_WRITER_HPP
