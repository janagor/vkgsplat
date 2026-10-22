#include <vkgsplat_capture/screenshot.hpp>

#include <expected>
#include <string_view>

#include <vkgsplat/frame.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_io/write_png.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::capture {

auto SavePng(Renderer &renderer, std::string_view path) -> std::expected<void, Error>
{
  auto frame = renderer.capture_latest_frame();
  if (!frame) { return std::unexpected{ frame.error() }; }
  return WritePng(*frame, path);
}

auto WritePng(RgbaFrame const &frame, std::string_view path) -> std::expected<void, Error>
{
  constexpr u32 kRgbaChannels = 4U;
  return vkgsplat::WritePng(path, frame.width, frame.height, kRgbaChannels, frame.pixels);
}

}// namespace vkgsplat::capture
