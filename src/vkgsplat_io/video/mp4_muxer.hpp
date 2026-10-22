#ifndef VKGSPLAT_IO_VIDEO_MP4_MUXER_HPP
#define VKGSPLAT_IO_VIDEO_MP4_MUXER_HPP

#include "video_sink.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

#include <vkgsplat_video_io/vkgsplat_video_io_export.hpp>

struct AVFormatContext;
struct AVStream;

namespace vkgsplat {

class VKGSPLAT_VIDEO_IO_EXPORT Mp4Muxer final : public VideoSink
{
public:
  [[nodiscard]] static auto Open(std::filesystem::path path, std::uint32_t width, std::uint32_t height,
    std::uint32_t fps_numerator, std::uint32_t fps_denominator,
    std::span<std::byte const> parameter_sets) -> std::expected<std::unique_ptr<Mp4Muxer>, Error>;

  ~Mp4Muxer() override;
  Mp4Muxer(Mp4Muxer const &) = delete;
  auto operator=(Mp4Muxer const &) -> Mp4Muxer & = delete;
  Mp4Muxer(Mp4Muxer &&) = delete;
  auto operator=(Mp4Muxer &&) -> Mp4Muxer & = delete;

  auto Write(EncodedPacket const &encoded) -> std::expected<void, Error> override;
  auto Finish() -> std::expected<void, Error> override;

private:
  Mp4Muxer(std::filesystem::path path, std::uint32_t fps_numerator, std::uint32_t fps_denominator) noexcept;

  std::filesystem::path path_;
  std::filesystem::path temporary_path_;
  AVFormatContext *context_ = nullptr;
  AVStream *stream_ = nullptr;
  std::uint32_t fps_numerator_{};
  std::uint32_t fps_denominator_{};
  std::uint64_t next_index_{};
  std::int64_t next_minimum_pts_{};
  bool finished_ = false;
  bool failed_ = false;
  std::optional<Error> publish_failure_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_IO_VIDEO_MP4_MUXER_HPP
