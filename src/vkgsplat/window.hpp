#pragma once

#include <vulkan/vulkan_core.h>

struct GLFWwindow;

namespace vkgsplat {

[[nodiscard]] auto create_window_glfw(const char *window_name = "", bool resize = true) -> GLFWwindow *;

void destroy_window_glfw(GLFWwindow *window);

[[nodiscard]] auto create_surface_glfw(VkInstance instance,
  GLFWwindow *window,
  VkAllocationCallbacks *allocator = nullptr) -> VkSurfaceKHR;

}// namespace vkgsplat
