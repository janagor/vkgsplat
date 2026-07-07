#pragma once


#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

#include <backend/vulkan/gpu_allocator.hpp>

namespace vkgsplat {

struct Init
{
  GLFWwindow *window{};
  vkb::Instance instance{};
  vkb::InstanceDispatchTable inst_disp;
  VkSurfaceKHR surface{};
  vkb::Device device{};
  vkb::DispatchTable disp;
  vkb::Swapchain swapchain{};
  vulkan::GPUAllocator gpu_allocator;
  PFN_vkWriteResourceDescriptorsEXT write_resource_descriptors{};
  PFN_vkCmdBindResourceHeapEXT cmd_bind_resource_heap{};
};


}// namespace vkgsplat
