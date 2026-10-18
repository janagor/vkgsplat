#ifndef VKGSPLAT_HEADLESS_PLATFORM_HPP
#define VKGSPLAT_HEADLESS_PLATFORM_HPP

#include <expected>

#include <vkgsplat/platform.hpp>
#include <vkgsplat_headless/vkgsplat_headless_platform_export.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

inline constexpr u32 kDefaultHeadlessWidth = 1920;
inline constexpr u32 kDefaultHeadlessHeight = 1080;

struct HeadlessPlatformConfig
{
  u32 width = kDefaultHeadlessWidth;
  u32 height = kDefaultHeadlessHeight;
};

class VKGSPLAT_HEADLESS_PLATFORM_EXPORT HeadlessPlatform final : public Platform
{
public:
  [[nodiscard]] static auto create(HeadlessPlatformConfig const &config) -> std::expected<HeadlessPlatform, Error>;
  [[nodiscard]] auto surface_mode() const noexcept -> SurfaceMode override;
  [[nodiscard]] auto native_window() const noexcept -> NativeWindowHandle override;
  [[nodiscard]] auto framebuffer_extent() const noexcept -> Extent2D override;
  void poll_events() const noexcept override;
  [[nodiscard]] auto should_close() const noexcept -> bool override;

private:
  explicit HeadlessPlatform(Extent2D extent) : extent_(extent) {}
  Extent2D extent_;
};

}// namespace vkgsplat
#endif
