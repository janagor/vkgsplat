#include "io/image/write_png.hpp"

#include <cstdint>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <stb_image_write.h>

namespace vkgsplat {

auto WritePng(std::string_view path,
  u32 width,
  u32 height,
  u32 channels,
  std::span<std::uint8_t const> pixels) -> std::expected<void, Error>
{
  if (width == 0 || height == 0) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "PNG width/height must be non-zero") };
  }
  if (channels < 1 || channels > 4) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "PNG channels must be 1..4") };
  }

  auto const expected_bytes =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels);
  if (pixels.size() < expected_bytes) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "PNG pixel buffer is too small") };
  }

  std::string const filename{ path };
  auto const stride = static_cast<int>(width * channels);
  if (stbi_write_png(filename.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        static_cast<int>(channels),
        pixels.data(),
        stride)
      == 0) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to write PNG: " + filename) };
  }

  return {};
}

}// namespace vkgsplat
