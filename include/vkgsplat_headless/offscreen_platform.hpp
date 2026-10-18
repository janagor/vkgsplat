#ifndef VKGSPLAT_OFFSCREEN_PLATFORM_HPP
#define VKGSPLAT_OFFSCREEN_PLATFORM_HPP

#include <expected>

#include <vkgsplat/platform.hpp>
#include <vkgsplat_headless/headless_platform.hpp>

namespace vkgsplat {

class VKGSPLAT_HEADLESS_PLATFORM_EXPORT OffscreenPlatform final : public Platform
{
public:
  [[nodiscard]] static auto create(HeadlessPlatformConfig const &config) -> std::expected<OffscreenPlatform, Error>;
  [[nodiscard]] auto surface_mode() const noexcept -> SurfaceMode override;
  [[nodiscard]] auto native_window() const noexcept -> NativeWindowHandle override;
  [[nodiscard]] auto framebuffer_extent() const noexcept -> Extent2D override;
  auto poll_events() const noexcept -> void override;
  [[nodiscard]] auto should_close() const noexcept -> bool override;

private:
  explicit OffscreenPlatform(Extent2D extent) : extent_(extent) {}
  Extent2D extent_;
};

}// namespace vkgsplat
#endif
