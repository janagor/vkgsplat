#include <vkgsplat_capture/frame_sequence.hpp>

#include "png_sequence_writer.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>

#include <limits>
#include <optional>
#include <utility>

namespace vkgsplat::capture {

struct FrameSequence::Impl
{
  Renderer *renderer;
  std::unique_ptr<PngSequenceWriter> writer;
  FrameSequenceSummary summary;
  bool finished = false;
  std::optional<Error> error;
};

FrameSequence::FrameSequence(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
FrameSequence::FrameSequence(FrameSequence &&) noexcept = default;
auto FrameSequence::operator=(FrameSequence &&) noexcept -> FrameSequence & = default;
FrameSequence::~FrameSequence() noexcept = default;

auto FrameSequence::create(Renderer &renderer, FrameSequenceConfig config) -> std::expected<FrameSequence, Error>
{
  auto const rate = config.frame_rate;
  auto writer = PngSequenceWriter::create(std::move(config));
  if (!writer) { return std::unexpected{ writer.error() }; }
  return FrameSequence{ std::make_unique<Impl>(Impl{ .renderer = &renderer,
    .writer = std::move(*writer), .summary = { .frame_rate = rate }, .finished = false, .error = {} }) };
}

auto FrameSequence::record_frame(Camera const &camera) -> std::expected<FrameMetadata, Error>
{
  if (!impl_ || impl_->finished) {
    return std::unexpected{ MakeError(std::errc::operation_not_permitted, "Frame sequence is finished") };
  }
  if (impl_->error) { return std::unexpected{ *impl_->error }; }
  if (impl_->summary.frame_count == std::numeric_limits<u64>::max()) {
    impl_->error = MakeError(std::errc::value_too_large, "Frame index overflow");
    return std::unexpected{ *impl_->error };
  }
  auto frame = impl_->renderer->render_frame(camera);
  if (!frame) {
    impl_->error = frame.error();
    return std::unexpected{ *impl_->error };
  }
  if (impl_->summary.frame_count != 0U
      && (frame->width != impl_->summary.width || frame->height != impl_->summary.height)) {
    impl_->error = MakeError(std::errc::invalid_argument, "Frame dimensions changed during sequence");
    return std::unexpected{ *impl_->error };
  }
  auto const width = frame->width;
  auto const height = frame->height;
  auto const index = impl_->summary.frame_count;
  auto path = impl_->writer->submit(FramePacket{ .sequence_index = index, .frame = std::move(*frame) });
  if (!path) {
    impl_->error = path.error();
    return std::unexpected{ *impl_->error };
  }
  impl_->summary.width = width;
  impl_->summary.height = height;
  ++impl_->summary.frame_count;
  return FrameMetadata{ .sequence_index = index, .path = std::move(*path), .width = width, .height = height };
}

auto FrameSequence::finish() -> std::expected<FrameSequenceSummary, Error>
{
  if (!impl_) { return std::unexpected{ MakeError(std::errc::operation_not_permitted, "Frame sequence was moved") }; }
  impl_->finished = true;
  auto result = impl_->writer->finish();
  if (!result) { return std::unexpected{ result.error() }; }
  if (impl_->error) { return std::unexpected{ *impl_->error }; }
  impl_->summary.queue_wait_nanoseconds = impl_->writer->queue_wait_nanoseconds();
  impl_->summary.peak_queued_frames = impl_->writer->peak_queued_frames();
  return impl_->summary;
}

}// namespace vkgsplat::capture
