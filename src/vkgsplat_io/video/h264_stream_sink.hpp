#ifndef VKGSPLAT_IO_VIDEO_H264_STREAM_SINK_HPP
#define VKGSPLAT_IO_VIDEO_H264_STREAM_SINK_HPP

#include "h264_writer.hpp"
#include "video_sink.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <utility>

#include <vkgsplat_io/vkgsplat_io_export.hpp>

namespace vkgsplat {

class VKGSPLAT_IO_EXPORT H264StreamSink final : public VideoSink
{
public:
  [[nodiscard]] static auto Open(std::filesystem::path const &path, std::span<std::byte const> parameter_sets)
    -> std::expected<H264StreamSink, Error>;

  auto Write(EncodedPacket const &packet) -> std::expected<void, Error> override;
  auto Finish() -> std::expected<void, Error> override;

private:
  explicit H264StreamSink(H264Writer writer) : writer_{ std::move(writer) } {}

  H264Writer writer_;
  std::uint64_t next_index_{};
  bool finished_ = false;
};

}// namespace vkgsplat

#endif// VKGSPLAT_IO_VIDEO_H264_STREAM_SINK_HPP
