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
