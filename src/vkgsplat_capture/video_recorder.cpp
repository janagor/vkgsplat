#include <vkgsplat_capture/video_recorder.hpp>
#include <vkgsplat_capture/video_config.hpp>
#include "video_recorder_test_access.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_video/recording_session.hpp>

#include "video/h264_stream_sink.hpp"
#include "video/video_sink.hpp"
#ifdef VKGSPLAT_ENABLE_MP4
#include "video/mp4_muxer.hpp"
#endif

#include <cstdint>
#include <cstddef>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::capture {
namespace {

  [[nodiscard]] auto Validate(VideoConfig const &config) -> std::expected<void, Error>
  {
    if (config.output_path.empty() || config.width == 0U || config.height == 0U
        || config.frame_rate.numerator == 0U || config.frame_rate.denominator == 0U
        || config.frame_rate.numerator > static_cast<u32>(std::numeric_limits<int>::max())
        || config.frame_rate.denominator > static_cast<u32>(std::numeric_limits<int>::max())) {
      return std::unexpected(MakeError(std::errc::invalid_argument,
        "video output path, extent and rational frame rate must be valid"));
    }
    if (config.gop.enable_p_frames || config.gop.idr_interval != 1U) {
      return std::unexpected(MakeError(std::errc::function_not_supported,
        "this H.264 encoder supports IDR-only recording; disable P frames and use IDR interval 1"));
    }
    if (config.rate_control.mode != RateControlMode::kDefault) {
      return std::unexpected(MakeError(std::errc::function_not_supported,
        "explicit video rate-control modes are not implemented"));
    }
    if (config.container != VideoContainer::kH264 && config.container != VideoContainer::kMp4) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "unknown video container"));
    }
#ifndef VKGSPLAT_ENABLE_MP4
    if (config.container == VideoContainer::kMp4) {
      return std::unexpected(MakeError(std::errc::function_not_supported,
        "MP4 output requires a build with vkgsplat_ENABLE_MP4"));
    }
#endif
    return {};
  }

  [[nodiscard]] auto OpenSink(VideoConfig const &config, std::span<std::byte const> parameter_sets)
    -> std::expected<std::unique_ptr<VideoSink>, Error>
  {
    if (config.container == VideoContainer::kH264) {
      auto raw = H264StreamSink::Open(config.output_path, parameter_sets);
      if (!raw) { return std::unexpected(raw.error()); }
      return std::unique_ptr<VideoSink>{ std::make_unique<H264StreamSink>(std::move(*raw)) };
    }
#ifdef VKGSPLAT_ENABLE_MP4
    auto mp4 = Mp4Muxer::Open(config.output_path, config.width, config.height,
      config.frame_rate.numerator, config.frame_rate.denominator, parameter_sets);
    if (!mp4) { return std::unexpected(mp4.error()); }
    return std::unique_ptr<VideoSink>{ std::move(*mp4) };
#else
    return std::unexpected(MakeError(std::errc::function_not_supported, "MP4 output is disabled"));
#endif
  }

}// namespace

struct VideoRecorder::Impl
{
  Impl(std::unique_ptr<VideoSink> output, std::unique_ptr<video::RecordingSession> recording)
    : sink{ std::move(output) }, session{ std::move(recording) }
  {}

  std::unique_ptr<VideoSink> sink;
  std::unique_ptr<video::RecordingSession> session;
  u64 next_index = 0U;
  bool finished = false;
  std::optional<Error> failure;
};

VideoRecorder::VideoRecorder(std::unique_ptr<Impl> impl) noexcept : impl_{ std::move(impl) } {}
VideoRecorder::VideoRecorder(VideoRecorder &&) noexcept = default;
auto VideoRecorder::operator=(VideoRecorder &&) noexcept -> VideoRecorder & = default;
VideoRecorder::~VideoRecorder() noexcept = default;

auto VideoRecorder::create(Renderer &renderer, VideoConfig const &config) -> std::expected<VideoRecorder, Error>
{
  if (auto valid = Validate(config); !valid) { return std::unexpected(valid.error()); }
  auto session = renderer.create_video_recording_session(config.width, config.height,
    config.frame_rate.numerator, config.frame_rate.denominator);
  if (!session) { return std::unexpected(session.error()); }
  auto sink = OpenSink(config, (*session)->ParameterSets());
  if (!sink) { return std::unexpected(sink.error()); }
  return VideoRecorder{ std::make_unique<Impl>(std::move(*sink), std::move(*session)) };
}

auto VideoRecorderTestAccess::Create(std::unique_ptr<video::RecordingSession> session,
  std::unique_ptr<VideoSink> sink) -> VideoRecorder
{
  return VideoRecorder{ std::make_unique<VideoRecorder::Impl>(std::move(sink), std::move(session)) };
}

auto VideoRecorder::record_frame(Camera const &camera) -> std::expected<void, Error>
{
  if (!impl_ || impl_->finished) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted, "video recorder is finished"));
  }
  if (impl_->failure) { return std::unexpected(*impl_->failure); }
  if (impl_->next_index > static_cast<u64>(std::numeric_limits<std::int64_t>::max())) {
    impl_->failure = MakeError(std::errc::value_too_large, "video frame index overflow");
    return std::unexpected(*impl_->failure);
  }
  auto packet = impl_->session->CaptureFrame(camera, impl_->next_index);
  if (!packet) {
    impl_->failure = packet.error();
    return std::unexpected(*impl_->failure);
  }
  if (auto written = impl_->sink->Write(*packet); !written) {
    impl_->failure = written.error();
    return std::unexpected(*impl_->failure);
  }
  ++impl_->next_index;
  return {};
}

auto VideoRecorder::finish() -> std::expected<void, Error>
{
  if (!impl_ || impl_->finished) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted, "video recorder is finished"));
  }
  if (impl_->failure) { return std::unexpected(*impl_->failure); }
  if (impl_->next_index == 0U) {
    impl_->failure = MakeError(std::errc::invalid_argument, "video recording has no frames");
    return std::unexpected(*impl_->failure);
  }
  if (auto finished = impl_->session->Finish(); !finished) {
    impl_->failure = finished.error();
    return std::unexpected(*impl_->failure);
  }
  if (auto finished = impl_->sink->Finish(); !finished) {
    impl_->failure = finished.error();
    return std::unexpected(*impl_->failure);
  }
  impl_->finished = true;
  return {};
}

}// namespace vkgsplat::capture
