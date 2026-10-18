#include <vkgsplat/platform.hpp>
#include <vkgsplat_headless/headless_platform.hpp>
#include <vkgsplat_headless/offscreen_platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <expected>
#include <system_error>

namespace vkgsplat {

auto OffscreenPlatform::create(HeadlessPlatformConfig const &config) -> std::expected<OffscreenPlatform, Error>
{
  if (config.width == 0U || config.height == 0U) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "Offscreen extent must be positive") };
  }
  return OffscreenPlatform{ Extent2D{ .width = config.width, .height = config.height } };
}

auto OffscreenPlatform::surface_mode() const noexcept -> SurfaceMode { return SurfaceMode::kOffscreen; }
auto OffscreenPlatform::native_window() const noexcept -> NativeWindowHandle { return nullptr; }
auto OffscreenPlatform::framebuffer_extent() const noexcept -> Extent2D { return extent_; }
auto OffscreenPlatform::poll_events() const noexcept -> void {}
auto OffscreenPlatform::should_close() const noexcept -> bool { return false; }

}// namespace vkgsplat
