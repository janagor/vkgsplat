#ifndef VKGSPLAT_PLATFORM_HPP
#define VKGSPLAT_PLATFORM_HPP

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

/** Opaque OS-native window handle, such as `GLFWwindow*`. */
using NativeWindowHandle = void *;

/**
 * OS windowing abstraction used by the renderer.
 *
 * Surface creation is performed by the Driver from `native_window()`. This
 * interface intentionally exposes no graphics API types, so the application
 * layer can remain independent of Vulkan.
 */
class Platform
{
public:
  virtual ~Platform() = default;
  Platform() = default;
  Platform(Platform const & /*other*/) = default;
  Platform(Platform && /*other*/) = default;
  auto operator=(Platform const & /*other*/) -> Platform & = default;
  auto operator=(Platform && /*other*/) -> Platform & = default;

  [[nodiscard]] virtual auto native_window() const noexcept -> NativeWindowHandle = 0;
  [[nodiscard]] virtual auto framebuffer_extent() const noexcept -> Extent2D = 0;
  virtual void poll_events() const noexcept = 0;
  [[nodiscard]] virtual auto should_close() const noexcept -> bool = 0;
};

}// namespace vkgsplat

#endif// VKGSPLAT_PLATFORM_HPP
