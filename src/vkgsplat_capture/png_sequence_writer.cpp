#include "png_sequence_writer.hpp"

#include <vkgsplat_capture/frame_sequence.hpp>
#include <vkgsplat_capture/screenshot.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::capture {
namespace {
  constexpr std::size_t kRgbaChannels = 4U;
  constexpr u32 kMaxFilenameDigits = 20U;

  class TemporaryFileGuard
  {
  public:
    explicit TemporaryFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryFileGuard() noexcept
    {
      if (active_) {
        std::error_code error;
        std::filesystem::remove(path_, error);
      }
    }
    TemporaryFileGuard(TemporaryFileGuard const &) = delete;
    auto operator=(TemporaryFileGuard const &) -> TemporaryFileGuard & = delete;
    TemporaryFileGuard(TemporaryFileGuard &&) = delete;
    auto operator=(TemporaryFileGuard &&) -> TemporaryFileGuard & = delete;
    auto dismiss() noexcept -> void { active_ = false; }

  private:
    std::filesystem::path path_;
    bool active_ = true;
  };

  [[nodiscard]] auto ValidateConfig(FrameSequenceConfig const &config) -> std::expected<void, Error>
  {
    if (config.output_directory.empty() || config.filename_prefix.empty() || config.filename_prefix == "."
        || config.filename_prefix == ".." || config.filename_prefix.contains('/')
        || config.filename_prefix.contains('\\') || config.frame_rate.numerator == 0U
        || config.frame_rate.denominator == 0U || config.filename_digits == 0U
        || config.filename_digits > kMaxFilenameDigits || config.max_pending_frames == 0U) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "Invalid frame sequence configuration") };
    }
    return {};
  }
}// namespace

PngSequenceWriter::PngSequenceWriter(FrameSequenceConfig config, WriteOperation write, QueueFullObserver on_queue_full)
  : config_(std::move(config)), write_(std::move(write)), on_queue_full_(std::move(on_queue_full))
{
  if (!write_) {
    write_ = [](RgbaFrame const &frame, std::filesystem::path const &path) -> std::expected<void, Error> {
      return WritePng(frame, path.string());
    };
  }
  worker_ = std::jthread([this]() -> void { run(); });
}

PngSequenceWriter::~PngSequenceWriter() noexcept
{
  {
    std::scoped_lock const lock{ mutex_ };
    closing_ = true;
  }
  available_.notify_all();
  capacity_.notify_all();
  if (worker_.joinable()) { worker_.join(); }
}

auto PngSequenceWriter::create(FrameSequenceConfig config, WriteOperation write, QueueFullObserver on_queue_full)
  -> std::expected<std::unique_ptr<PngSequenceWriter>, Error>
{
  if (auto valid = ValidateConfig(config); !valid) { return std::unexpected{ valid.error() }; }
  std::error_code filesystem_error;
  if (std::filesystem::exists(config.output_directory, filesystem_error)) {
    if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot inspect output directory") }; }
    auto const is_directory = std::filesystem::is_directory(config.output_directory, filesystem_error);
    if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot inspect output directory") }; }
    if (!is_directory) {
      return std::unexpected{ MakeError(std::errc::not_a_directory, "Sequence output is not a directory") };
    }
    auto const is_empty = std::filesystem::is_empty(config.output_directory, filesystem_error);
    if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot inspect output directory") }; }
    if (!is_empty) {
      return std::unexpected{ MakeError(std::errc::file_exists, "Sequence output directory must be empty") };
    }
  } else {
    if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot inspect output directory") }; }
    std::filesystem::create_directories(config.output_directory, filesystem_error);
    if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot create output directory") }; }
  }
  return std::unique_ptr<PngSequenceWriter>{ new PngSequenceWriter(
    std::move(config), std::move(write), std::move(on_queue_full)) };
}

auto PngSequenceWriter::path_for(u64 sequence_index) const -> std::filesystem::path
{
  std::ostringstream name;
  name << config_.filename_prefix << std::setw(static_cast<int>(config_.filename_digits)) << std::setfill('0')
       << (config_.start_index + sequence_index) << ".png";
  return config_.output_directory / name.str();
}

auto PngSequenceWriter::submit(FramePacket packet) -> std::expected<std::filesystem::path, Error>
{
  if (packet.frame.width == 0U || packet.frame.height == 0U
      || static_cast<size_t>(packet.frame.width)
           > std::numeric_limits<size_t>::max() / kRgbaChannels / packet.frame.height
      || packet.frame.pixels.size() != static_cast<size_t>(packet.frame.width) * packet.frame.height * kRgbaChannels) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "Invalid RGBA frame") };
  }
  std::unique_lock lock{ mutex_ };
  auto const wait_started = std::chrono::steady_clock::now();
  auto const was_full = queue_.size() >= config_.max_pending_frames;
  if (was_full && on_queue_full_) { on_queue_full_(); }
  capacity_.wait(
    lock, [this]() -> bool { return queue_.size() < config_.max_pending_frames || error_.has_value() || closing_; });
  if (was_full) {
    auto const waited = std::chrono::steady_clock::now() - wait_started;
    queue_wait_nanoseconds_ += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(waited).count());
  }
  if (error_) { return std::unexpected{ *error_ }; }
  if (closing_ || finished_) {
    return std::unexpected{ MakeError(std::errc::operation_not_permitted, "Sequence writer is finished") };
  }
  if (packet.sequence_index != next_index_ || packet.sequence_index == std::numeric_limits<u64>::max()
      || packet.sequence_index > std::numeric_limits<u64>::max() - config_.start_index) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "Frame index is out of order or overflows") };
  }
  auto path = path_for(packet.sequence_index);
  std::error_code filesystem_error;
  auto const occupied = std::filesystem::exists(path, filesystem_error);
  if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot inspect output frame") }; }
  if (occupied) { return std::unexpected{ MakeError(std::errc::file_exists, "Output frame already exists") }; }
  queue_.push_back(std::move(packet));
  peak_queued_frames_ = std::max(peak_queued_frames_, queue_.size());
  ++next_index_;
  lock.unlock();
  available_.notify_one();
  return path;
}

auto PngSequenceWriter::write_packet(FramePacket const &packet) -> std::expected<void, Error>
{
  auto const path = path_for(packet.sequence_index);
  auto temporary = path;
  temporary += ".part";
  std::error_code filesystem_error;
  auto const occupied = std::filesystem::exists(path, filesystem_error);
  if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot inspect output frame") }; }
  auto const temporary_occupied = std::filesystem::exists(temporary, filesystem_error);
  if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot inspect temporary frame") }; }
  if (occupied || temporary_occupied) {
    return std::unexpected{ MakeError(std::errc::file_exists, "Output frame already exists") };
  }
  TemporaryFileGuard cleanup{ temporary };
  auto written = write_(packet.frame, temporary);
  if (!written) { return std::unexpected{ written.error() }; }
  std::filesystem::create_hard_link(temporary, path, filesystem_error);
  if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot publish PNG frame") }; }
  std::filesystem::remove(temporary, filesystem_error);
  if (filesystem_error) { return std::unexpected{ MakeError(filesystem_error, "Cannot remove temporary PNG") }; }
  cleanup.dismiss();
  return {};
}

auto PngSequenceWriter::run() -> void
{
  for (;;) {
    std::unique_lock lock{ mutex_ };
    available_.wait(lock, [this]() -> bool { return !queue_.empty() || closing_; });
    if (queue_.empty()) { return; }
    auto packet = std::move(queue_.front());
    queue_.pop_front();
    lock.unlock();
    capacity_.notify_one();
    std::optional<Error> write_error;
    try {
      auto result = write_packet(packet);
      if (!result) { write_error = result.error(); }
    } catch (std::exception const &exception) {
      write_error = MakeError(std::errc::io_error, exception.what());
    } catch (...) {
      write_error = MakeError(std::errc::io_error, "PNG writer failed");
    }
    if (write_error) {
      lock.lock();
      error_ = *write_error;
      queue_.clear();
      closing_ = true;
      lock.unlock();
      capacity_.notify_all();
      available_.notify_all();
      return;
    }
  }
}

auto PngSequenceWriter::queue_wait_nanoseconds() const noexcept -> u64 { return queue_wait_nanoseconds_; }
auto PngSequenceWriter::peak_queued_frames() const noexcept -> size_t { return peak_queued_frames_; }

auto PngSequenceWriter::finish() -> std::expected<void, Error>
{
  {
    std::scoped_lock const lock{ mutex_ };
    closing_ = true;
  }
  available_.notify_all();
  capacity_.notify_all();
  if (worker_.joinable()) { worker_.join(); }
  finished_ = true;
  if (error_) { return std::unexpected{ *error_ }; }
  return {};
}

}// namespace vkgsplat::capture
