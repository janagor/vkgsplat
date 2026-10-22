#ifndef VKGSPLAT_CAPTURE_SCREENSHOT_HPP
#define VKGSPLAT_CAPTURE_SCREENSHOT_HPP

#include <expected>
#include <string_view>

#include <vkgsplat_capture/vkgsplat_capture_export.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

class Renderer;
struct RgbaFrame;

namespace capture {

  [[nodiscard]] VKGSPLAT_CAPTURE_EXPORT auto SavePng(Renderer &renderer, std::string_view path)
    -> std::expected<void, Error>;

  [[nodiscard]] VKGSPLAT_CAPTURE_EXPORT auto WritePng(RgbaFrame const &frame, std::string_view path)
    -> std::expected<void, Error>;

}// namespace capture
}// namespace vkgsplat

#endif// VKGSPLAT_CAPTURE_SCREENSHOT_HPP
