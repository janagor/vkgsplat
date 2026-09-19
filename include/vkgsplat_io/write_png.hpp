#ifndef VKGSPLAT_IO_WRITE_PNG_HPP
#define VKGSPLAT_IO_WRITE_PNG_HPP

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

/**
 * Write tightly packed 8-bit pixels in row-major order with a top-left origin.
 * `channels` must be 1 (Y), 2 (YA), 3 (RGB), or 4 (RGBA), and `pixels` must
 * contain `width * height * channels` bytes.
 */
[[nodiscard]] auto
  WritePng(std::string_view path, u32 width, u32 height, u32 channels, std::span<std::uint8_t const> pixels)
    -> std::expected<void, Error>;

}// namespace vkgsplat

#endif// VKGSPLAT_IO_WRITE_PNG_HPP
