#include "h264_stream_sink.hpp"

#include "h264_writer.hpp"

#include <vkgsplat_video/encoded_packet.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

auto H264StreamSink::Open(std::filesystem::path const &path,
  std::span<std::byte const> parameter_sets) -> std::expected<H264StreamSink, Error>
{
  if (path.empty()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "H.264 output path is empty"));
  }
  auto writer = H264Writer::Open(path.string(), parameter_sets);
  if (!writer) { return std::unexpected(writer.error()); }
  return H264StreamSink{ std::move(*writer) };
}

auto H264StreamSink::Write(EncodedPacket const &packet) -> std::expected<void, Error>
{
  constexpr std::int64_t kFrameDuration = 1;
  if (finished_ || packet.frame_index != next_index_
      || packet.frame_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
      || std::cmp_not_equal(packet.pts, packet.frame_index)
      || packet.dts != packet.pts || packet.duration != kFrameDuration || packet.data.empty()
      || (next_index_ == 0U && !packet.keyframe)) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid or out-of-order H.264 packet"));
  }
  auto result = writer_.WriteFrame(packet.data);
  if (!result) { return result; }
  ++next_index_;
  return {};
}

auto H264StreamSink::Finish() -> std::expected<void, Error>
{
  if (finished_ || next_index_ == 0U) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted, "H.264 output has no frames or is finalized"));
  }
  auto result = writer_.Finish();
  if (!result) { return result; }
  finished_ = true;
  return {};
}

}// namespace vkgsplat
