#pragma once

#include <utility>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

class CommandBuffer
{
public:
  CommandBuffer() noexcept = default;

  explicit CommandBuffer(VkCommandBuffer handle) noexcept : handle_(handle) {}

  CommandBuffer(CommandBuffer &&other) noexcept : handle_(std::exchange(other.handle_, VK_NULL_HANDLE)) {}

  auto operator=(CommandBuffer &&other) noexcept -> CommandBuffer &
  {
    if (this != &other) { handle_ = std::exchange(other.handle_, VK_NULL_HANDLE); }
    return *this;
  }

  CommandBuffer(CommandBuffer const &) = delete;
  auto operator=(CommandBuffer const &) -> CommandBuffer & = delete;

  [[nodiscard]] auto handle() const noexcept -> VkCommandBuffer { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != VK_NULL_HANDLE; }

private:
  VkCommandBuffer handle_{ VK_NULL_HANDLE };
};

}// namespace vkgsplat::vulkan
