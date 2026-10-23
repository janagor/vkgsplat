// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include "../src/vkgsplat_io/video/mp4_muxer.hpp"
#include <vkgsplat_video/encoded_packet.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr std::array kParameterSets{
  std::byte{ 0x00 },
  std::byte{ 0x00 },
  std::byte{ 0x00 },
  std::byte{ 0x01 },
  std::byte{ 0x67 },
  std::byte{ 0x42 },
  std::byte{ 0xc0 },
  std::byte{ 0x0a },
  std::byte{ 0xdd },
  std::byte{ 0xec },
  std::byte{ 0x04 },
  std::byte{ 0x40 },
  std::byte{ 0x00 },
  std::byte{ 0x00 },
  std::byte{ 0x03 },
  std::byte{ 0x00 },
  std::byte{ 0x40 },
  std::byte{ 0x00 },
  std::byte{ 0x00 },
  std::byte{ 0x03 },
  std::byte{ 0x00 },
  std::byte{ 0x83 },
  std::byte{ 0xc4 },
  std::byte{ 0x89 },
  std::byte{ 0xe0 },
  std::byte{ 0x00 },
  std::byte{ 0x00 },
  std::byte{ 0x00 },
  std::byte{ 0x01 },
  std::byte{ 0x68 },
  std::byte{ 0xce },
  std::byte{ 0x0f },
  std::byte{ 0xc8 },
};
constexpr std::uint32_t kVideoWidth = 16U;
constexpr std::uint32_t kVideoHeight = 16U;
constexpr std::uint32_t kFrameRateNumerator = 30U;
constexpr std::uint32_t kFrameRateDenominator = 1U;
constexpr std::ptrdiff_t kReservedTemporaryFileCount = 1;
constexpr std::ptrdiff_t kSingleOutputFileCount = 1;
constexpr std::ptrdiff_t kOccupiedOutputAndTemporaryFileCount = 2;
constexpr char kOccupiedOutputByte = 'x';
constexpr std::array kIdrPicture{
  std::byte{ 0x00 },
  std::byte{ 0x00 },
  std::byte{ 0x01 },
  std::byte{ 0x65 },
  std::byte{ 0x88 },
};

class TemporaryDirectory
{
public:
  TemporaryDirectory()
  {
    auto const suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("vkgsplat-mp4-cleanup-" + std::to_string(suffix));
    std::filesystem::create_directory(path_);
  }
  ~TemporaryDirectory()
  {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  TemporaryDirectory(TemporaryDirectory const &) = delete;
  auto operator=(TemporaryDirectory const &) -> TemporaryDirectory & = delete;
  TemporaryDirectory(TemporaryDirectory &&) = delete;
  auto operator=(TemporaryDirectory &&) -> TemporaryDirectory & = delete;
  [[nodiscard]] auto path() const -> std::filesystem::path const & { return path_; }

private:
  std::filesystem::path path_;
};

}// namespace

TEST_CASE("failed unfinished MP4 removes its temporary output", "[video]")
{
  TemporaryDirectory const directory;
  auto const output = directory.path() / "unfinished.mp4";
  {
    auto muxer = vkgsplat::Mp4Muxer::Open(
      output, kVideoWidth, kVideoHeight, kFrameRateNumerator, kFrameRateDenominator, kParameterSets);
    REQUIRE(muxer);
    REQUIRE_FALSE((*muxer)->Write(vkgsplat::EncodedPacket{}));
    REQUIRE_FALSE((*muxer)->Finish());
    REQUIRE_FALSE(std::filesystem::exists(output));
    auto const entry_count =
      std::distance(std::filesystem::directory_iterator{ directory.path() }, std::filesystem::directory_iterator{});
    REQUIRE(entry_count == kReservedTemporaryFileCount);
  }
  REQUIRE_FALSE(std::filesystem::exists(output));
  REQUIRE(std::filesystem::is_empty(directory.path()));
}

TEST_CASE("MP4 publication failure is sticky and removes temporary output", "[video]")
{
  TemporaryDirectory const directory;
  auto const output = directory.path() / "occupied.mp4";
  {
    auto muxer = vkgsplat::Mp4Muxer::Open(
      output, kVideoWidth, kVideoHeight, kFrameRateNumerator, kFrameRateDenominator, kParameterSets);
    REQUIRE(muxer);
    vkgsplat::EncodedPacket const packet{
      .data = std::vector<std::byte>{ kIdrPicture.begin(), kIdrPicture.end() },
      .frame_index = 0U,
      .pts = 0,
      .dts = 0,
      .duration = 1,
      .keyframe = true,
    };
    REQUIRE((*muxer)->Write(packet));
    {
      std::ofstream occupied{ output, std::ios::binary };
      REQUIRE(occupied);
      occupied.put(kOccupiedOutputByte);
      REQUIRE(occupied);
    }
    auto failed = (*muxer)->Finish();
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error().code() == std::errc::file_exists);
    auto repeated = (*muxer)->Finish();
    REQUIRE_FALSE(repeated);
    REQUIRE(repeated.error().code() == failed.error().code());
    REQUIRE(repeated.error().message() == failed.error().message());
    auto written = (*muxer)->Write(packet);
    REQUIRE_FALSE(written);
    REQUIRE(written.error().code() == failed.error().code());
    auto const entry_count =
      std::distance(std::filesystem::directory_iterator{ directory.path() }, std::filesystem::directory_iterator{});
    REQUIRE(entry_count == kOccupiedOutputAndTemporaryFileCount);
  }
  REQUIRE(std::filesystem::exists(output));
  std::ifstream occupied{ output, std::ios::binary };
  REQUIRE(occupied);
  REQUIRE(occupied.get() == kOccupiedOutputByte);
  REQUIRE(occupied.peek() == std::char_traits<char>::eof());
  auto const entry_count =
    std::distance(std::filesystem::directory_iterator{ directory.path() }, std::filesystem::directory_iterator{});
  REQUIRE(entry_count == kSingleOutputFileCount);
}

TEST_CASE("successful MP4 publication removes its temporary output", "[video]")
{
  TemporaryDirectory const directory;
  auto const output = directory.path() / "completed.mp4";
  {
    auto muxer = vkgsplat::Mp4Muxer::Open(
      output, kVideoWidth, kVideoHeight, kFrameRateNumerator, kFrameRateDenominator, kParameterSets);
    REQUIRE(muxer);
    vkgsplat::EncodedPacket const packet{
      .data = std::vector<std::byte>{ kIdrPicture.begin(), kIdrPicture.end() },
      .frame_index = 0U,
      .pts = 0,
      .dts = 0,
      .duration = 1,
      .keyframe = true,
    };
    REQUIRE((*muxer)->Write(packet));
    REQUIRE((*muxer)->Finish());
    REQUIRE(std::filesystem::exists(output));
    auto const entry_count =
      std::distance(std::filesystem::directory_iterator{ directory.path() }, std::filesystem::directory_iterator{});
    REQUIRE(entry_count == kSingleOutputFileCount);
  }
  REQUIRE(std::filesystem::exists(output));
  auto const entry_count =
    std::distance(std::filesystem::directory_iterator{ directory.path() }, std::filesystem::directory_iterator{});
  REQUIRE(entry_count == kSingleOutputFileCount);
}

TEST_CASE("MP4 accepts timestamp gaps with consecutive encode indices", "[video]")
{
  TemporaryDirectory const directory;
  auto const output = directory.path() / "timestamp-gaps.mp4";
  auto muxer = vkgsplat::Mp4Muxer::Open(
    output, kVideoWidth, kVideoHeight, kFrameRateNumerator, kFrameRateDenominator, kParameterSets);
  REQUIRE(muxer);
  constexpr std::int64_t kFirstTimestamp = 0;
  constexpr std::int64_t kSecondTimestamp = 3;
  constexpr std::int64_t kFrameDuration = 1;
  vkgsplat::EncodedPacket const first{
    .data = std::vector<std::byte>{ kIdrPicture.begin(), kIdrPicture.end() },
    .frame_index = 0U,
    .pts = kFirstTimestamp,
    .dts = kFirstTimestamp,
    .duration = kFrameDuration,
    .keyframe = true,
  };
  REQUIRE((*muxer)->Write(first));
  auto second = first;
  second.frame_index = 1U;
  second.pts = kSecondTimestamp;
  second.dts = kSecondTimestamp;
  REQUIRE((*muxer)->Write(second));
  REQUIRE((*muxer)->Finish());
  REQUIRE(std::filesystem::exists(output));
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
