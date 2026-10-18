#include <vkgsplat/platform.hpp>
#include <vkgsplat_headless/headless_platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <expected>
#include <system_error>

namespace vkgsplat {

auto HeadlessPlatform::create(HeadlessPlatformConfig const &config) -> std::expected<HeadlessPlatform, Error>
{
  if (config.width == 0 || config.height == 0) {
    return std::unexpected{ MakeError(
      std::errc::invalid_argument, "Headless extent must have positive width and height") };
  }
  return HeadlessPlatform{ Extent2D{ .width = config.width, .height = config.height } };
}

auto HeadlessPlatform::surface_mode() const noexcept -> SurfaceMode { return SurfaceMode::kHeadless; }
auto HeadlessPlatform::native_window() const noexcept -> NativeWindowHandle { return nullptr; }
auto HeadlessPlatform::framebuffer_extent() const noexcept -> Extent2D { return extent_; }
void HeadlessPlatform::poll_events() const noexcept {}
auto HeadlessPlatform::should_close() const noexcept -> bool { return false; }

}// namespace vkgsplat
