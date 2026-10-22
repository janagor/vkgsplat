// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include <vkgsplat/frame.hpp>
#include <vkgsplat_capture/screenshot.hpp>
#include <vkgsplat_utility/types.hpp>

#include <catch2/catch_test_macros.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace {

constexpr vkgsplat::u32 kWidth = 2U;
constexpr vkgsplat::u32 kHeight = 2U;
constexpr vkgsplat::u32 kSinglePixelDimension = 1U;
constexpr int kRgbaChannels = 4;
constexpr std::size_t kPixelBytes = static_cast<std::size_t>(kWidth) * kHeight * kRgbaChannels;

class TemporaryPng
{
public:
  TemporaryPng()
  {
    auto const suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("vkgsplat-capture-" + std::to_string(suffix) + ".png");
  }

  ~TemporaryPng()
  {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  TemporaryPng(TemporaryPng const &) = delete;
  auto operator=(TemporaryPng const &) -> TemporaryPng & = delete;
  TemporaryPng(TemporaryPng &&) = delete;
  auto operator=(TemporaryPng &&) -> TemporaryPng & = delete;

  [[nodiscard]] auto path() const -> std::filesystem::path const & { return path_; }

private:
  std::filesystem::path path_;
};

}// namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("capture writes RGBA pixels with the original dimensions", "[capture]")
{
  std::array<std::uint8_t, kPixelBytes> const expected{
    255U,
    0U,
    0U,
    255U,
    0U,
    255U,
    0U,
    128U,
    0U,
    0U,
    255U,
    64U,
    255U,
    255U,
    255U,
    0U,
  };
  vkgsplat::RgbaFrame const frame{ .width = kWidth, .height = kHeight, .pixels = { expected.begin(), expected.end() } };
  TemporaryPng const output;
  auto const filename = output.path().string();

  REQUIRE(vkgsplat::capture::WritePng(frame, filename));
  int width{};
  int height{};
  int channels{};
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> const pixels{
    stbi_load(filename.c_str(), &width, &height, &channels, kRgbaChannels), stbi_image_free
  };
  REQUIRE(pixels != nullptr);
  CHECK(std::cmp_equal(width, kWidth));
  CHECK(std::cmp_equal(height, kHeight));
  CHECK(channels == kRgbaChannels);
  std::span<std::uint8_t const> const pixel_bytes{ pixels.get(), expected.size() };
  CHECK(std::ranges::equal(pixel_bytes, expected));
}

TEST_CASE("capture propagates PNG validation and output errors", "[capture]")
{
  TemporaryPng const output;
  vkgsplat::RgbaFrame const empty{};
  auto invalid_dimensions = vkgsplat::capture::WritePng(empty, output.path().string());
  REQUIRE_FALSE(invalid_dimensions);
  CHECK(invalid_dimensions.error().code() == std::make_error_code(std::errc::invalid_argument));

  vkgsplat::RgbaFrame const short_pixels{ .width = kWidth, .height = kHeight, .pixels = {} };
  auto invalid_pixels = vkgsplat::capture::WritePng(short_pixels, output.path().string());
  REQUIRE_FALSE(invalid_pixels);
  CHECK(invalid_pixels.error().code() == std::make_error_code(std::errc::invalid_argument));

  vkgsplat::RgbaFrame const valid{
    .width = kSinglePixelDimension, .height = kSinglePixelDimension, .pixels = { 0U, 0U, 0U, 255U }
  };
  auto invalid_path = vkgsplat::capture::WritePng(valid, (output.path() / "missing.png").string());
  REQUIRE_FALSE(invalid_path);
  CHECK(invalid_path.error().code() == std::make_error_code(std::errc::io_error));
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
