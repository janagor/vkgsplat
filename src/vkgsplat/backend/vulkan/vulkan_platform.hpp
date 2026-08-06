#ifndef VKGSPLAT_BACKEND_VULKAN_VULKAN_PLATFORM_HPP
#define VKGSPLAT_BACKEND_VULKAN_VULKAN_PLATFORM_HPP

#include <expected>

#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

[[nodiscard]] auto CreateSurfaceFromNativeWindow(VkInstance instance, NativeWindowHandle native_window)
  -> std::expected<VkSurfaceKHR, Error>;

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_VULKAN_PLATFORM_HPP
