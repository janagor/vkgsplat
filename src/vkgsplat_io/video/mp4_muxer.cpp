#include "mp4_muxer.hpp"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <vkgsplat_video/encoded_packet.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/defs.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {
namespace {

  constexpr std::uint8_t kH264NalTypeMask = 0x1f;
  constexpr std::uint8_t kSpsNalType = 7;
  constexpr std::uint8_t kPpsNalType = 8;
  constexpr std::size_t kLongStartCodeSize = 4;
  constexpr std::size_t kShortStartCodeSize = 3;
  constexpr std::uint32_t kMaxTemporaryPathAttempts = 256U;
  [[nodiscard]] auto TemporaryPathSequence() noexcept -> std::atomic_uint64_t &
  {
    static std::atomic_uint64_t sequence{ 0U };
    return sequence;
  }

  [[nodiscard]] auto FfmpegError(char const *operation, int code) -> Error
  {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(code, buffer.data(), buffer.size());
    return MakeError(std::errc::io_error, std::string{ operation } + ": " + buffer.data());
  }

  [[nodiscard]] auto HasParameterSet(std::span<std::byte const> data, std::uint8_t type) -> bool
  {
    for (std::size_t index = 0; index + kShortStartCodeSize < data.size(); ++index) {
      auto const bytes = data.subspan(index);
      if (bytes.front() != std::byte{} || bytes.subspan(1U).front() != std::byte{}) { continue; }
      auto const long_code = index + kLongStartCodeSize < data.size()
                             && bytes.subspan(2U).front() == std::byte{}
                             && bytes.subspan(kShortStartCodeSize).front() == std::byte{ 1 };
      auto const short_code = bytes.subspan(2U).front() == std::byte{ 1 };
      if ((long_code || short_code)
          && (std::to_integer<std::uint8_t>(bytes.subspan(long_code ? kLongStartCodeSize : kShortStartCodeSize).front())
              & kH264NalTypeMask) == type) {
        return true;
      }
    }
    return false;
  }

}// namespace

Mp4Muxer::Mp4Muxer(std::filesystem::path path, std::uint32_t fps_numerator,
  std::uint32_t fps_denominator) noexcept
  : path_{ std::move(path) }, fps_numerator_{ fps_numerator }, fps_denominator_{ fps_denominator }
{}

Mp4Muxer::~Mp4Muxer()
{
  if (context_ != nullptr) {
    if ((static_cast<unsigned int>(context_->oformat->flags) & static_cast<unsigned int>(AVFMT_NOFILE)) == 0U
        && context_->pb != nullptr) {
      avio_closep(&context_->pb);
    }
    avformat_free_context(context_);
  }
  if (!temporary_path_.empty()) {
    std::error_code error;
    std::filesystem::remove(temporary_path_, error);
  }
}

auto Mp4Muxer::Open(std::filesystem::path path, std::uint32_t width, std::uint32_t height,
  std::uint32_t fps_numerator, std::uint32_t fps_denominator,
  std::span<std::byte const> parameter_sets) -> std::expected<std::unique_ptr<Mp4Muxer>, Error>
{
  if (path.empty() || width == 0 || height == 0 || fps_numerator == 0 || fps_denominator == 0
      || width > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || fps_numerator > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || fps_denominator > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
      || parameter_sets.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())
      || !HasParameterSet(parameter_sets, kSpsNalType) || !HasParameterSet(parameter_sets, kPpsNalType)) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid MP4 video parameters or SPS/PPS"));
  }
  std::error_code path_error;
  if (std::filesystem::exists(path, path_error)) {
    return std::unexpected(MakeError(std::errc::file_exists, "MP4 output already exists"));
  }
  if (path_error) { return std::unexpected(MakeError(path_error, "inspect MP4 output path failed")); }
  auto muxer = std::unique_ptr<Mp4Muxer>{ new Mp4Muxer{ std::move(path), fps_numerator, fps_denominator } };
  for (std::uint32_t attempt = 0U; attempt < kMaxTemporaryPathAttempts; ++attempt) {
    auto candidate = muxer->path_;
    candidate += ".partial." + std::to_string(TemporaryPathSequence().fetch_add(1U));
    std::ofstream const reservation{ candidate, std::ios::out | std::ios::binary | std::ios::noreplace };
    if (reservation) {
      muxer->temporary_path_ = std::move(candidate);
      break;
    }
    std::error_code candidate_error;
    if (!std::filesystem::exists(candidate, candidate_error) || candidate_error) {
      return std::unexpected(MakeError(std::errc::io_error, "reserve MP4 temporary output failed"));
    }
  }
  if (muxer->temporary_path_.empty()) {
    return std::unexpected(MakeError(std::errc::file_exists, "no free MP4 temporary output name"));
  }
  auto const output = muxer->temporary_path_.string();
  auto result = avformat_alloc_output_context2(&muxer->context_, nullptr, "mp4", output.c_str());
  if (result < 0 || muxer->context_ == nullptr) {
    return std::unexpected(FfmpegError("allocate MP4 context", result));
  }
  muxer->stream_ = avformat_new_stream(muxer->context_, nullptr);
  if (muxer->stream_ == nullptr) {
    return std::unexpected(MakeError(std::errc::not_enough_memory, "create MP4 video stream failed"));
  }
  auto *parameters = muxer->stream_->codecpar;
  parameters->codec_type = AVMEDIA_TYPE_VIDEO;
  parameters->codec_id = AV_CODEC_ID_H264;
  parameters->width = static_cast<int>(width);
  parameters->height = static_cast<int>(height);
  parameters->codec_tag = 0;
  parameters->extradata = static_cast<std::uint8_t *>(av_mallocz(parameter_sets.size() + AV_INPUT_BUFFER_PADDING_SIZE));
  if (parameters->extradata == nullptr) {
    return std::unexpected(MakeError(std::errc::not_enough_memory, "allocate MP4 codec data failed"));
  }
  std::memcpy(parameters->extradata, parameter_sets.data(), parameter_sets.size());
  parameters->extradata_size = static_cast<int>(parameter_sets.size());
  muxer->stream_->time_base = AVRational{ .num = static_cast<int>(fps_denominator),
    .den = static_cast<int>(fps_numerator) };
  muxer->stream_->avg_frame_rate = AVRational{ .num = static_cast<int>(fps_numerator),
    .den = static_cast<int>(fps_denominator) };
  if ((static_cast<unsigned int>(muxer->context_->oformat->flags) & static_cast<unsigned int>(AVFMT_NOFILE)) == 0U) {
    result = avio_open(&muxer->context_->pb, output.c_str(), AVIO_FLAG_WRITE);
    if (result < 0) { return std::unexpected(FfmpegError("open MP4 output", result)); }
  }
  result = avformat_write_header(muxer->context_, nullptr);
  if (result < 0) { return std::unexpected(FfmpegError("write MP4 header", result)); }
  return muxer;
}

auto Mp4Muxer::Write(EncodedPacket const &encoded) -> std::expected<void, Error>
{
  if (publish_failure_) { return std::unexpected(*publish_failure_); }
  if (finished_ || failed_ || context_ == nullptr) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted, "MP4 output is finalized"));
  }
  if (encoded.frame_index != next_index_ || encoded.pts != encoded.dts
      || encoded.pts < next_minimum_pts_
      || encoded.frame_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
      || encoded.duration <= 0
      || encoded.pts > std::numeric_limits<std::int64_t>::max() - encoded.duration
      || encoded.data.empty()
      || encoded.data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())
      || (next_index_ == 0 && !encoded.keyframe)) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid or out-of-order MP4 video packet"));
  }
  auto *packet = av_packet_alloc();
  if (packet == nullptr) { return std::unexpected(MakeError(std::errc::not_enough_memory, "allocate MP4 packet failed")); }
  auto const allocated = av_new_packet(packet, static_cast<int>(encoded.data.size()));
  if (allocated < 0) {
    av_packet_free(&packet);
    return std::unexpected(FfmpegError("allocate MP4 packet data", allocated));
  }
  std::memcpy(packet->data, encoded.data.data(), encoded.data.size());
  packet->pts = encoded.pts;
  packet->dts = encoded.dts;
  packet->duration = encoded.duration;
  packet->stream_index = stream_->index;
  if (encoded.keyframe) {
    packet->flags = static_cast<int>(static_cast<unsigned int>(packet->flags)
                                     | static_cast<unsigned int>(AV_PKT_FLAG_KEY));
  }
  av_packet_rescale_ts(packet,
    AVRational{ .num = static_cast<int>(fps_denominator_), .den = static_cast<int>(fps_numerator_) },
    stream_->time_base);
  auto const result = av_interleaved_write_frame(context_, packet);
  av_packet_free(&packet);
  if (result < 0) {
    failed_ = true;
    return std::unexpected(FfmpegError("write MP4 packet", result));
  }
  next_minimum_pts_ = encoded.pts + encoded.duration;
  ++next_index_;
  return {};
}

auto Mp4Muxer::Finish() -> std::expected<void, Error>
{
  if (publish_failure_) { return std::unexpected(*publish_failure_); }
  if (finished_ || failed_ || context_ == nullptr) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted, "MP4 output is finalized"));
  }
  if (next_index_ == 0) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "MP4 recording contains no frames"));
  }
  auto const result = av_write_trailer(context_);
  if (result < 0) {
    failed_ = true;
    return std::unexpected(FfmpegError("write MP4 trailer", result));
  }
  if ((static_cast<unsigned int>(context_->oformat->flags) & static_cast<unsigned int>(AVFMT_NOFILE)) == 0U
      && context_->pb != nullptr) {
    auto const closed = avio_closep(&context_->pb);
    if (closed < 0) {
      failed_ = true;
      return std::unexpected(FfmpegError("close MP4 output", closed));
    }
  }
  std::error_code error;
  // The reserved temporary file is in the destination directory, so publication
  // uses a same-filesystem hard link and never replaces an existing output.
  std::filesystem::create_hard_link(temporary_path_, path_, error);
  if (error) {
    failed_ = true;
    publish_failure_ = MakeError(error, "publish MP4 output failed");
    return std::unexpected(*publish_failure_);
  }
  finished_ = true;
  std::filesystem::remove(temporary_path_, error);
  if (!error) { temporary_path_.clear(); }
  return {};
}

}// namespace vkgsplat
