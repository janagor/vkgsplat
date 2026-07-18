#include "window.hpp"

#include <iostream>
#include <print>
#include <utility>

#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace vkgsplat {

auto create_window_glfw(const char *window_name, bool resize) -> GLFWwindow *
{
  if (glfwInit() == 0) {
    std::println("Failed to initialize GLFW");
    return nullptr;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  if (!resize) { glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); }

  auto const k_window_extent = std::pair<int, int>{ 1024, 1024 };
  return glfwCreateWindow(k_window_extent.first, k_window_extent.second, window_name, nullptr, nullptr);
}

void destroy_window_glfw(GLFWwindow *window)
{
  glfwDestroyWindow(window);
  glfwTerminate();
}

auto create_surface_glfw(VkInstance instance, GLFWwindow *window, VkAllocationCallbacks *allocator) -> VkSurfaceKHR
{
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkResult const err = glfwCreateWindowSurface(instance, window, allocator, &surface);
  if (0 != err) {
    char const *error_msg = nullptr;
    int const ret = glfwGetError(&error_msg);
    if (ret != 0) {
      std::cout << ret << " ";
      if (error_msg != nullptr) { std::cout << error_msg; }
      std::cout << "\n";
    }
    surface = VK_NULL_HANDLE;
  }
  return surface;
}

}// namespace vkgsplat
