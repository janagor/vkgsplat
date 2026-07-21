#include "backend/vulkan/command/pool.hpp"

#include <algorithm>
#include <expected>
#include <functional>
#include <system_error>
#include <utility>
#include <vector>

#include "backend/vulkan/command/buffer.hpp"
#include "backend/vulkan/initializers.hpp"
#include <vkgsplat/error.hpp>
#include <vkgsplat/types.hpp>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

CommandPool::CommandPool(VkCommandPool pool, std::reference_wrapper<vkb::DispatchTable> disp) noexcept
  : pool_(pool), disp_(disp)
{}

CommandPool::CommandPool(CommandPool &&other) noexcept
  : pool_(std::exchange(other.pool_, VK_NULL_HANDLE)), disp_(other.disp_)
{}

auto CommandPool::operator=(CommandPool &&other) noexcept -> CommandPool &
{
  if (this != &other) {
    cleanup();
    pool_ = std::exchange(other.pool_, VK_NULL_HANDLE);
    disp_ = other.disp_;
  }
  return *this;
}

CommandPool::~CommandPool() noexcept { cleanup(); }

void CommandPool::cleanup() noexcept
{
  if (pool_ != VK_NULL_HANDLE) { disp_.get().destroyCommandPool(pool_, nullptr); }
  pool_ = VK_NULL_HANDLE;
}

auto CommandPool::create(std::reference_wrapper<vkb::DispatchTable> disp,
  u32 queue_family_index,
  VkCommandPoolCreateFlags flags) -> std::expected<CommandPool, Error>
{
  auto const pool_info = initializers::CommandPoolCreateInfo(queue_family_index, flags);
  VkCommandPool pool = VK_NULL_HANDLE;
  if (disp.get().createCommandPool(&pool_info, nullptr, &pool) != VK_SUCCESS) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::io_error), "Failed to create command pool" } };
  }
  return CommandPool(pool, disp);
}

auto CommandPool::allocate_buffers(u32 count, VkCommandBufferLevel level) const
  -> std::expected<std::vector<CommandBuffer>, Error>
{
  std::vector<VkCommandBuffer> handles(count);
  auto const alloc_info = initializers::CommandBufferAllocateInfo(pool_, level, count);
  if (disp_.get().allocateCommandBuffers(&alloc_info, handles.data()) != VK_SUCCESS) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::io_error), "Failed to allocate command buffers" } };
  }

  std::vector<CommandBuffer> buffers(handles.size());
  std::ranges::transform(handles, buffers.begin(), [](VkCommandBuffer vk_handle) { return CommandBuffer(vk_handle); });
  return buffers;
}

}// namespace vkgsplat::vulkan
