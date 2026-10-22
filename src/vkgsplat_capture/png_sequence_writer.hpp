#ifndef VKGSPLAT_CAPTURE_PNG_SEQUENCE_WRITER_HPP
#define VKGSPLAT_CAPTURE_PNG_SEQUENCE_WRITER_HPP

#include <condition_variable>
#include <deque>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include <vkgsplat/frame.hpp>
#include <vkgsplat_capture/frame_sequence.hpp>

namespace vkgsplat::capture {

struct FramePacket
{
  u64 sequence_index{};
  RgbaFrame frame;
};

class PngSequenceWriter
{
public:
  using WriteOperation = std::function<std::expected<void, Error>(RgbaFrame const &, std::filesystem::path const &)>;
  // Internal test hook. Called with mutex_ held; it must not re-enter or throw.
  using QueueFullObserver = std::function<void()>;

  [[nodiscard]] static auto create(FrameSequenceConfig config,
    WriteOperation write = {},
    QueueFullObserver on_queue_full = {}) -> std::expected<std::unique_ptr<PngSequenceWriter>, Error>;
  [[nodiscard]] auto submit(FramePacket packet) -> std::expected<std::filesystem::path, Error>;
  [[nodiscard]] auto finish() -> std::expected<void, Error>;
  [[nodiscard]] auto path_for(u64 sequence_index) const -> std::filesystem::path;
  [[nodiscard]] auto queue_wait_nanoseconds() const noexcept -> u64;
  [[nodiscard]] auto peak_queued_frames() const noexcept -> size_t;
  ~PngSequenceWriter() noexcept;

  PngSequenceWriter(PngSequenceWriter const &) = delete;
  auto operator=(PngSequenceWriter const &) -> PngSequenceWriter & = delete;
  PngSequenceWriter(PngSequenceWriter &&) = delete;
  auto operator=(PngSequenceWriter &&) -> PngSequenceWriter & = delete;

private:
  explicit PngSequenceWriter(FrameSequenceConfig config, WriteOperation write, QueueFullObserver on_queue_full);
  auto run() -> void;
  [[nodiscard]] auto write_packet(FramePacket const &packet) -> std::expected<void, Error>;

  FrameSequenceConfig config_;
  WriteOperation write_;
  QueueFullObserver on_queue_full_;
  std::jthread worker_;
  std::mutex mutex_;
  std::condition_variable available_;
  std::condition_variable capacity_;
  std::deque<FramePacket> queue_;
  std::optional<Error> error_;
  bool closing_ = false;
  bool finished_ = false;
  u64 next_index_ = 0U;
  u64 queue_wait_nanoseconds_ = 0U;
  size_t peak_queued_frames_ = 0U;
};

}// namespace vkgsplat::capture

#endif// VKGSPLAT_CAPTURE_PNG_SEQUENCE_WRITER_HPP
