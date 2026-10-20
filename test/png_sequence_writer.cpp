#include "../src/vkgsplat_capture/png_sequence_writer.hpp"

#include <vkgsplat/frame.hpp>
#include <vkgsplat_capture/screenshot.hpp>

#include <catch2/catch_test_macros.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace {
constexpr vkgsplat::u32 kWidth = 2U;
constexpr vkgsplat::u32 kHeight = 1U;
constexpr vkgsplat::u32 kChannels = 4U;
constexpr vkgsplat::u32 kFrameCount = 10U;
constexpr vkgsplat::u32 kFirstFileIndex = 7U;
constexpr vkgsplat::u32 kFilenameDigits = 3U;

class TemporaryDirectory
{
public:
  TemporaryDirectory()
  {
    auto const suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("vkgsplat-sequence-" + std::to_string(suffix));
  }
  ~TemporaryDirectory()
  {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  TemporaryDirectory(TemporaryDirectory const &) = delete;
  auto operator=(TemporaryDirectory const &) -> TemporaryDirectory & = delete;
  [[nodiscard]] auto path() const -> std::filesystem::path const & { return path_; }
private:
  std::filesystem::path path_;
};

[[nodiscard]] auto MakeFrame(std::uint8_t value) -> vkgsplat::RgbaFrame
{
  return { .width = kWidth, .height = kHeight,
    .pixels = { value, 0U, 0U, 255U, value, 0U, 0U, 255U } };
}

class WriteGate
{
public:
  auto enter_and_wait() -> void
  {
    std::unique_lock lock{ mutex_ };
    entered_ = true;
    condition_.notify_all();
    condition_.wait(lock, [this]() -> bool { return released_; });
  }
  auto wait_until_entered() -> void
  {
    std::unique_lock lock{ mutex_ };
    condition_.wait(lock, [this]() -> bool { return entered_; });
  }
  auto release() -> void
  {
    {
      std::lock_guard const lock{ mutex_ };
      released_ = true;
    }
    condition_.notify_all();
  }
private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool entered_ = false;
  bool released_ = false;
};
}// namespace

TEST_CASE("PNG sequence preserves order and pixels with a bounded queue", "[capture]")
{
  TemporaryDirectory const output;
  auto writer = vkgsplat::capture::PngSequenceWriter::create({
    .output_directory = output.path(), .start_index = kFirstFileIndex,
    .filename_digits = kFilenameDigits, .max_pending_frames = 1U,
  });
  REQUIRE(writer);
  for (vkgsplat::u64 index = 0U; index < kFrameCount; ++index) {
    auto path = (*writer)->submit({ .sequence_index = index, .frame = MakeFrame(static_cast<std::uint8_t>(index)) });
    REQUIRE(path);
    CHECK(path->filename() == std::format("frame_{:03}.png", kFirstFileIndex + index));
  }
  REQUIRE((*writer)->finish());
  REQUIRE((*writer)->finish());
  for (vkgsplat::u64 index = 0U; index < kFrameCount; ++index) {
    auto const path = (*writer)->path_for(index).string();
    int width{};
    int height{};
    int channels{};
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
      stbi_load(path.c_str(), &width, &height, &channels, static_cast<int>(kChannels)), stbi_image_free
    };
    REQUIRE(pixels != nullptr);
    CHECK(width == static_cast<int>(kWidth));
    CHECK(height == static_cast<int>(kHeight));
    CHECK(pixels.get()[0] == static_cast<std::uint8_t>(index));
  }
  CHECK_FALSE((*writer)->submit({ .sequence_index = kFrameCount, .frame = MakeFrame(0U) }));
}

TEST_CASE("PNG sequence rejects invalid frames and occupied output", "[capture]")
{
  TemporaryDirectory const output;
  auto writer = vkgsplat::capture::PngSequenceWriter::create({ .output_directory = output.path() });
  REQUIRE(writer);
  CHECK_FALSE((*writer)->submit({ .sequence_index = 0U, .frame = {} }));
  REQUIRE((*writer)->submit({ .sequence_index = 0U, .frame = MakeFrame(1U) }));
  REQUIRE((*writer)->finish());
  auto occupied = vkgsplat::capture::PngSequenceWriter::create({ .output_directory = output.path() });
  CHECK_FALSE(occupied);
}

TEST_CASE("PNG writer failure reaches finish and remains failed", "[capture]")
{
  TemporaryDirectory const output;
  auto writer = vkgsplat::capture::PngSequenceWriter::create({ .output_directory = output.path() },
    [](vkgsplat::RgbaFrame const &, std::filesystem::path const &) -> std::expected<void, vkgsplat::Error> {
      return std::unexpected{ vkgsplat::MakeError(std::errc::io_error, "Injected write failure") };
    });
  REQUIRE(writer);
  REQUIRE((*writer)->submit({ .sequence_index = 0U, .frame = MakeFrame(1U) }));
  auto failed = (*writer)->finish();
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code() == std::make_error_code(std::errc::io_error));
  CHECK_FALSE((*writer)->finish());
  CHECK_FALSE(std::filesystem::exists((*writer)->path_for(0U)));
}

TEST_CASE("PNG sequence can finish empty and rejects out-of-order frames", "[capture]")
{
  TemporaryDirectory const empty_output;
  auto empty = vkgsplat::capture::PngSequenceWriter::create({ .output_directory = empty_output.path() });
  REQUIRE(empty);
  REQUIRE((*empty)->finish());
  CHECK(std::filesystem::is_empty(empty_output.path()));

  TemporaryDirectory const ordered_output;
  auto ordered = vkgsplat::capture::PngSequenceWriter::create({ .output_directory = ordered_output.path() });
  REQUIRE(ordered);
  CHECK_FALSE((*ordered)->submit({ .sequence_index = 1U, .frame = MakeFrame(1U) }));
  REQUIRE((*ordered)->submit({ .sequence_index = 0U, .frame = MakeFrame(0U) }));
  CHECK_FALSE((*ordered)->submit({ .sequence_index = 0U, .frame = MakeFrame(0U) }));
  REQUIRE((*ordered)->finish());
}

TEST_CASE("PNG sequence rejects inconsistent RGBA buffer size", "[capture]")
{
  TemporaryDirectory const output;
  auto writer = vkgsplat::capture::PngSequenceWriter::create({ .output_directory = output.path() });
  REQUIRE(writer);
  auto frame = MakeFrame(1U);
  frame.pixels.pop_back();
  CHECK_FALSE((*writer)->submit({ .sequence_index = 0U, .frame = std::move(frame) }));
  REQUIRE((*writer)->finish());
  CHECK(std::filesystem::is_empty(output.path()));
}

TEST_CASE("PNG sequence does not overwrite a filename created after session start", "[capture]")
{
  TemporaryDirectory const output;
  auto writer = vkgsplat::capture::PngSequenceWriter::create({ .output_directory = output.path() });
  REQUIRE(writer);
  auto const occupied = (*writer)->path_for(0U);
  std::ofstream file{ occupied };
  REQUIRE(file.is_open());
  file << "existing";
  file.close();
  auto result = (*writer)->submit({ .sequence_index = 0U, .frame = MakeFrame(1U) });
  CHECK_FALSE(result);
  REQUIRE((*writer)->finish());
  CHECK(std::filesystem::file_size(occupied) == std::string{ "existing" }.size());
}

TEST_CASE("Destroying an unfinished PNG writer drains accepted frames", "[capture]")
{
  TemporaryDirectory const output;
  std::filesystem::path last_frame;
  {
    auto writer = vkgsplat::capture::PngSequenceWriter::create({
      .output_directory = output.path(), .max_pending_frames = 1U,
    });
    REQUIRE(writer);
    for (vkgsplat::u64 index = 0U; index < kFrameCount; ++index) {
      auto path = (*writer)->submit({ .sequence_index = index, .frame = MakeFrame(static_cast<std::uint8_t>(index)) });
      REQUIRE(path);
      last_frame = *path;
    }
  }
  CHECK(std::filesystem::is_regular_file(last_frame));
}

TEST_CASE("A full PNG queue blocks the producer until the worker advances", "[capture]")
{
  TemporaryDirectory const output;
  WriteGate gate;
  std::promise<void> queue_full;
  auto queue_full_future = queue_full.get_future();
  auto writer = vkgsplat::capture::PngSequenceWriter::create({
    .output_directory = output.path(), .max_pending_frames = 1U,
  }, [&gate](vkgsplat::RgbaFrame const &frame, std::filesystem::path const &path)
       -> std::expected<void, vkgsplat::Error> {
    gate.enter_and_wait();
    return vkgsplat::capture::write_png(frame, path.string());
  }, [&queue_full]() -> void { queue_full.set_value(); });
  REQUIRE(writer);
  REQUIRE((*writer)->submit({ .sequence_index = 0U, .frame = MakeFrame(0U) }));
  gate.wait_until_entered();
  REQUIRE((*writer)->submit({ .sequence_index = 1U, .frame = MakeFrame(1U) }));
  auto blocked = std::async(std::launch::async, [&writer]() -> std::expected<std::filesystem::path, vkgsplat::Error> {
    return (*writer)->submit({ .sequence_index = 2U, .frame = MakeFrame(2U) });
  });
  queue_full_future.wait();
  CHECK(blocked.wait_for(std::chrono::nanoseconds::zero()) == std::future_status::timeout);
  gate.release();
  REQUIRE(blocked.get());
  REQUIRE((*writer)->finish());
  CHECK(std::filesystem::is_regular_file((*writer)->path_for(2U)));
}

TEST_CASE("A throwing PNG writer cleans partial files and releases a blocked producer", "[capture]")
{
  TemporaryDirectory const output;
  WriteGate gate;
  std::promise<void> queue_full;
  auto queue_full_future = queue_full.get_future();
  auto writer = vkgsplat::capture::PngSequenceWriter::create({
    .output_directory = output.path(), .max_pending_frames = 1U,
  }, [&gate](vkgsplat::RgbaFrame const &, std::filesystem::path const &path)
       -> std::expected<void, vkgsplat::Error> {
    gate.enter_and_wait();
    std::ofstream partial{ path };
    partial << "partial";
    partial.close();
    throw std::runtime_error{ "Injected writer exception" };
  }, [&queue_full]() -> void { queue_full.set_value(); });
  REQUIRE(writer);
  REQUIRE((*writer)->submit({ .sequence_index = 0U, .frame = MakeFrame(0U) }));
  gate.wait_until_entered();
  REQUIRE((*writer)->submit({ .sequence_index = 1U, .frame = MakeFrame(1U) }));
  auto blocked = std::async(std::launch::async, [&writer]() -> std::expected<std::filesystem::path, vkgsplat::Error> {
    return (*writer)->submit({ .sequence_index = 2U, .frame = MakeFrame(2U) });
  });
  queue_full_future.wait();
  CHECK(blocked.wait_for(std::chrono::nanoseconds::zero()) == std::future_status::timeout);
  gate.release();
  CHECK_FALSE(blocked.get());
  auto failed = (*writer)->finish();
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message() == "Injected writer exception");
  CHECK(std::filesystem::is_empty(output.path()));
}

TEST_CASE("PNG filename padding has a practical upper bound", "[capture]")
{
  TemporaryDirectory const output;
  constexpr vkgsplat::u32 kExcessiveDigits = 21U;
  auto writer = vkgsplat::capture::PngSequenceWriter::create({
    .output_directory = output.path(), .filename_digits = kExcessiveDigits,
  });
  CHECK_FALSE(writer);
  CHECK_FALSE(std::filesystem::exists(output.path()));
}
