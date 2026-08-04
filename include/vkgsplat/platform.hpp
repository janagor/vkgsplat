#pragma once

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

// Opaque OS-native window handle (e.g. GLFWwindow*).
using NativeWindowHandle = void *;

// Platform abstracts OS windowing. Surface creation is performed by the Driver
// from a native window handle — platforms never expose graphics-API types.
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
