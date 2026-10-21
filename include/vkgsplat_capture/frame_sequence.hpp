#ifndef VKGSPLAT_CAPTURE_FRAME_SEQUENCE_HPP
#define VKGSPLAT_CAPTURE_FRAME_SEQUENCE_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include <vkgsplat_capture/vkgsplat_capture_export.hpp>
#include <vkgsplat_capture/frame_rate.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {
class Camera;
class Renderer;

namespace capture {

struct FrameSequenceConfig
{
  std::filesystem::path output_directory;
  std::string filename_prefix = "frame_";
  FrameRate frame_rate{};
  u64 start_index = 0U;
  u32 filename_digits = 6U;
  // Queued frames; the writer may own one additional frame while encoding.
  size_t max_pending_frames = 3U;
};

struct FrameMetadata
{
  // Zero-based presentation index. Timestamp is sequence_index * denominator / numerator seconds.
  u64 sequence_index{};
  std::filesystem::path path;
  u32 width{};
  u32 height{};
};

struct FrameSequenceSummary
{
  u64 frame_count{};
  u32 width{};
  u32 height{};
  FrameRate frame_rate{};
  // Time the producer spent waiting for queue capacity.
  u64 queue_wait_nanoseconds{};
  // Peak number of queued frames, excluding the frame being encoded.
  size_t peak_queued_frames{};
};

/** Single-producer capture session. The borrowed renderer must outlive this object. */
class VKGSPLAT_CAPTURE_EXPORT FrameSequence
{
public:
  [[nodiscard]] static auto create(Renderer &renderer, FrameSequenceConfig config)
    -> std::expected<FrameSequence, Error>;
  [[nodiscard]] auto record_frame(Camera const &camera) -> std::expected<FrameMetadata, Error>;
  [[nodiscard]] auto finish() -> std::expected<FrameSequenceSummary, Error>;

  FrameSequence(FrameSequence &&) noexcept;
  auto operator=(FrameSequence &&) noexcept -> FrameSequence &;
  FrameSequence(FrameSequence const &) = delete;
  auto operator=(FrameSequence const &) -> FrameSequence & = delete;
  ~FrameSequence() noexcept;

private:
  struct Impl;
  explicit FrameSequence(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}// namespace capture
}// namespace vkgsplat

#endif// VKGSPLAT_CAPTURE_FRAME_SEQUENCE_HPP
