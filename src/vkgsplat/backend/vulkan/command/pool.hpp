#ifndef VKGSPLAT_BACKEND_VULKAN_COMMAND_POOL_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMMAND_POOL_HPP

#include <expected>
#include <functional>
#include <vector>

#include "backend/vulkan/command/buffer.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

class CommandPool
{
public:
  CommandPool() = delete;
  ~CommandPool() noexcept;

  CommandPool(CommandPool const &) = delete;
  auto operator=(CommandPool const &) -> CommandPool & = delete;

  CommandPool(CommandPool &&other) noexcept;
  auto operator=(CommandPool &&other) noexcept -> CommandPool &;

  [[nodiscard]] static auto create(std::reference_wrapper<vkb::DispatchTable> disp,
    u32 queue_family_index,
    VkCommandPoolCreateFlags flags = 0) -> std::expected<CommandPool, Error>;

  [[nodiscard]] auto allocate_buffers(u32 count, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const
    -> std::expected<std::vector<CommandBuffer>, Error>;

  [[nodiscard]] auto handle() const noexcept -> VkCommandPool { return pool_; }
  [[nodiscard]] explicit operator bool() const noexcept { return pool_ != VK_NULL_HANDLE; }

private:
  CommandPool(VkCommandPool pool, std::reference_wrapper<vkb::DispatchTable> disp) noexcept;
  void cleanup() noexcept;

  VkCommandPool pool_{ VK_NULL_HANDLE };
  std::reference_wrapper<vkb::DispatchTable> disp_;
};

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_COMMAND_POOL_HPP
