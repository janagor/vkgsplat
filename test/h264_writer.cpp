#include "../src/vkgsplat_io/video/h264_writer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr std::array kParameterSets{
  std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }, std::byte{ 0x67 }, std::byte{ 0x42 },
  std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }, std::byte{ 0x68 }, std::byte{ 0xce },
};
constexpr std::array kIdrPicture{
  std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }, std::byte{ 0x65 }, std::byte{ 0x88 },
};
constexpr std::array kInvalidPicture{
  std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }, std::byte{ 0x41 }, std::byte{ 0x88 },
};
// SPS/PPS from a 16x16 Baseline H.264 frame encoded by FFmpeg/libx264.
constexpr std::array kRealParameterSets{
  std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x01 },
  std::byte{ 0x67 }, std::byte{ 0x42 }, std::byte{ 0xc0 }, std::byte{ 0x0a },
  std::byte{ 0xdd }, std::byte{ 0xec }, std::byte{ 0x04 }, std::byte{ 0x40 },
  std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x03 }, std::byte{ 0x00 },
  std::byte{ 0x40 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x03 },
  std::byte{ 0x00 }, std::byte{ 0x83 }, std::byte{ 0xc4 }, std::byte{ 0x89 },
  std::byte{ 0xe0 }, std::byte{ 0x00 }, std::byte{ 0x00 }, std::byte{ 0x00 },
  std::byte{ 0x01 }, std::byte{ 0x68 }, std::byte{ 0xce }, std::byte{ 0x0f },
  std::byte{ 0xc8 },
};

class TemporaryFile
{
public:
  TemporaryFile()
  {
    auto const suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("vkgsplat-h264-writer-" + std::to_string(suffix));
  }
  ~TemporaryFile()
  {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }
  TemporaryFile(TemporaryFile const &) = delete;
  auto operator=(TemporaryFile const &) -> TemporaryFile & = delete;
  [[nodiscard]] auto path() const -> std::filesystem::path const & { return path_; }
private:
  std::filesystem::path path_;
};

}// namespace

TEST_CASE("H.264 writer preserves Annex B NAL order and closes on finish", "[video]")
{
  TemporaryFile const file;
  auto writer = vkgsplat::H264Writer::Open(file.path().string(), kParameterSets);
  REQUIRE(writer);
  CHECK_FALSE(writer->WriteFrame(kInvalidPicture));
  REQUIRE(writer->WriteFrame(kIdrPicture));
  REQUIRE(writer->WriteFrame(kIdrPicture));
  REQUIRE(writer->Finish());
  CHECK_FALSE(writer->WriteFrame(kIdrPicture));
  CHECK_FALSE(writer->Finish());

  std::ifstream input{ file.path(), std::ios::binary };
  REQUIRE(input);
  std::vector<char> actual{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
  std::vector<char> expected;
  auto append = [&](auto const &bytes) -> void {
    for (auto byte : bytes) { expected.push_back(static_cast<char>(std::to_integer<unsigned char>(byte))); }
  };
  append(kParameterSets);
  append(kIdrPicture);
  append(kIdrPicture);
  CHECK(actual == expected);
}

TEST_CASE("H.264 writer rejects missing parameter sets", "[video]")
{
  TemporaryFile const file;
  CHECK_FALSE(vkgsplat::H264Writer::Open(file.path().string(), kIdrPicture));
  CHECK_FALSE(std::filesystem::exists(file.path()));
}

TEST_CASE("H.264 writer accepts complete externally generated parameter sets", "[video]")
{
  TemporaryFile const file;
  auto writer = vkgsplat::H264Writer::Open(file.path().string(), kRealParameterSets);
  REQUIRE(writer);
  REQUIRE(writer->Finish());
  CHECK(std::filesystem::file_size(file.path()) == kRealParameterSets.size());
}
