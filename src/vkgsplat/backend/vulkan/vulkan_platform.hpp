#pragma once

#include <expected>

#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

[[nodiscard]] auto create_surface_from_native_window(VkInstance instance, NativeWindowHandle native_window)
  -> std::expected<VkSurfaceKHR, Error>;

}// namespace vkgsplat::vulkan
