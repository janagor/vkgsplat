#pragma once

#include <expected>
#include <string>

#include <beman/indirect/indirect.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

struct WindowConfig
{
  std::string title = "vkgsplat";
  u32 width = 1024;
  u32 height = 1024;
  bool resizable = true;
};

// GLFW-backed Platform. No graphics-API types appear in this public header.
class Window : public Platform
{
public:
  [[nodiscard]] static auto create(WindowConfig const &config) -> std::expected<Window, Error>;

  Window(Window &&) noexcept;
  auto operator=(Window &&) noexcept -> Window &;
  ~Window() noexcept override;

  Window(Window const &) = delete;
  auto operator=(Window const &) -> Window & = delete;

  void poll_events() const noexcept override;
  [[nodiscard]] auto should_close() const noexcept -> bool override;
  [[nodiscard]] auto framebuffer_extent() const noexcept -> Extent2D override;
  [[nodiscard]] auto native_window() const noexcept -> NativeWindowHandle override;
  [[nodiscard]] auto native_handle() const noexcept -> void *;

private:
  struct Impl;
  explicit Window(beman::indirect::indirect<Impl> impl);

  beman::indirect::indirect<Impl> impl_;
};

}// namespace vkgsplat
