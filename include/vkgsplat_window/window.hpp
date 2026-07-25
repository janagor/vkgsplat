#pragma once

#include <expected>
#include <string>

#include <beman/indirect/indirect.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

struct WindowConfig
{
  std::string title = "vkgsplat";
  u32 width = 1024;
  u32 height = 1024;
  bool resizable = true;
};

class Window
{
public:
  [[nodiscard]] static auto create(WindowConfig const &config) -> std::expected<Window, Error>;

  Window(Window &&) noexcept;
  auto operator=(Window &&) noexcept -> Window &;
  ~Window();

  Window(Window const &) = delete;
  auto operator=(Window const &) -> Window & = delete;

  void poll_events() const;
  [[nodiscard]] auto should_close() const -> bool;
  [[nodiscard]] auto framebuffer_extent() const -> Extent2D;
  [[nodiscard]] auto create_surface(VkInstance instance) const -> std::expected<VkSurfaceKHR, Error>;
  [[nodiscard]] auto native_handle() const noexcept -> void *;

private:
  struct Impl;
  explicit Window(beman::indirect::indirect<Impl> impl);

  beman::indirect::indirect<Impl> impl_;
};

}// namespace vkgsplat
