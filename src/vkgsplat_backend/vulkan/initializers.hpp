#ifndef VKGSPLAT_BACKEND_VULKAN_INITIALIZERS_HPP
#define VKGSPLAT_BACKEND_VULKAN_INITIALIZERS_HPP

#include <span>

#include <vulkan/vulkan_core.h>

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::initializers {

inline auto CommandPoolCreateInfo(u32 queue_family_index, VkCommandPoolCreateFlags flags = 0) -> VkCommandPoolCreateInfo
{
  return VkCommandPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .queueFamilyIndex = queue_family_index,
  };
}

inline auto CommandBufferAllocateInfo(VkCommandPool command_pool, VkCommandBufferLevel level, u32 command_buffer_count)
  -> VkCommandBufferAllocateInfo
{
  return VkCommandBufferAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext = nullptr,
    .commandPool = command_pool,
    .level = level,
    .commandBufferCount = command_buffer_count,
  };
}

inline auto CommandBufferBeginInfo(VkCommandBufferUsageFlags flags = 0) -> VkCommandBufferBeginInfo
{
  return VkCommandBufferBeginInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = nullptr,
    .flags = flags,
    .pInheritanceInfo = nullptr,
  };
}

inline auto PresentInfoKHR(std::span<VkSemaphore const> wait_semaphores,
  std::span<VkSwapchainKHR const> swapchains,
  std::span<u32 const> image_indices) -> VkPresentInfoKHR
{
  return VkPresentInfoKHR{
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pNext = nullptr,
    .waitSemaphoreCount = static_cast<u32>(wait_semaphores.size()),
    .pWaitSemaphores = wait_semaphores.data(),
    .swapchainCount = static_cast<u32>(swapchains.size()),
    .pSwapchains = swapchains.data(),
    .pImageIndices = image_indices.data(),
    .pResults = nullptr,
  };
}

}// namespace vkgsplat::initializers

#endif// VKGSPLAT_BACKEND_VULKAN_INITIALIZERS_HPP
