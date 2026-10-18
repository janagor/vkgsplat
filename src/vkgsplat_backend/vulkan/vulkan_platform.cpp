#include "vulkan_platform.hpp"

#include <expected>
#include <string>
#include <system_error>
#include <utility>

#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>

#include <GLFW/glfw3.h>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

namespace {
  auto CreateHeadlessSurface(VkInstance instance) -> std::expected<VkSurfaceKHR, Error>
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const create_surface =
      reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT"));
    if (create_surface == nullptr) {
      return std::unexpected{ MakeError(std::errc::function_not_supported,
        "Headless rendering unavailable: vkCreateHeadlessSurfaceEXT entry point is missing") };
    }
    VkHeadlessSurfaceCreateInfoEXT const create_info{
      .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT, .pNext = nullptr, .flags = 0
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult const result = create_surface(instance, &create_info, nullptr, &surface);
    if (result != VK_SUCCESS) {
      return std::unexpected{ MakeError(std::errc::io_error,
        "vkCreateHeadlessSurfaceEXT failed with VkResult " + std::to_string(static_cast<int>(result))) };
    }
    return surface;
  }
}// namespace

auto CreateSurface(VkInstance instance, Platform const &platform) -> std::expected<VkSurfaceKHR, Error>
{
  switch (platform.surface_mode()) {
  case SurfaceMode::kWindowed:
    return CreateSurfaceFromNativeWindow(instance, platform.native_window());
  case SurfaceMode::kHeadless:
    return CreateHeadlessSurface(instance);
  }
  return std::unexpected{ MakeError(std::errc::invalid_argument, "Unknown Vulkan surface mode") };
}

auto CreateSurfaceFromNativeWindow(VkInstance instance, NativeWindowHandle native_window)
  -> std::expected<VkSurfaceKHR, Error>
{
  if (native_window == nullptr) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "Native window handle is null") };
  }

  auto *const glfw_window = static_cast<GLFWwindow *>(native_window);
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkResult const result = glfwCreateWindowSurface(instance, glfw_window, nullptr, &surface);
  if (result != VK_SUCCESS) {
    char const *glfw_message = nullptr;
    glfwGetError(&glfw_message);
    std::string message = "Failed to create Vulkan window surface";
    if (glfw_message != nullptr) {
      message += ": ";
      message += glfw_message;
    }
    return std::unexpected{ MakeError(std::errc::io_error, std::move(message)) };
  }
  return surface;
}

}// namespace vkgsplat::vulkan
