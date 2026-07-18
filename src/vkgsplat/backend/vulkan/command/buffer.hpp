#pragma once

#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

class CommandBuffer
{
public:
  CommandBuffer() = default;

  explicit CommandBuffer(VkCommandBuffer handle) noexcept : handle_(handle) {}

  [[nodiscard]] auto handle() const noexcept -> VkCommandBuffer { return handle_; }

private:
  VkCommandBuffer handle_{ VK_NULL_HANDLE };
};

}// namespace vkgsplat::vulkan
