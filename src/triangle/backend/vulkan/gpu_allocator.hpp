#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <expected>

namespace vkgsplat::vulkan {

struct Buffer
{
  VkBuffer handle = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
};

class [[nodiscard]] GPUAllocator
{
public:
  GPUAllocator() noexcept = default;
  ~GPUAllocator() noexcept;
  GPUAllocator(const GPUAllocator &) = delete;
  auto operator=(const GPUAllocator &) -> GPUAllocator & = delete;

  GPUAllocator(GPUAllocator &&other) noexcept;
  auto operator=(GPUAllocator &&other) noexcept -> GPUAllocator &;

  // TODO: add correct error
  static auto create(VkInstance instance, VkDevice device, VkPhysicalDevice physical_device) noexcept
    -> std::expected<GPUAllocator, void *>;

  auto create_storage_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, void *>;
  void destroy_buffer(Buffer &buffer) noexcept;
  auto map_buffer(Buffer const &buffer) noexcept -> std::expected<void *, void *>;
  void unmap_buffer(Buffer const &buffer) noexcept;

private:
  explicit GPUAllocator(VmaAllocator allocator) noexcept;

  VmaAllocator allocator_{ VK_NULL_HANDLE };
};

}// namespace vkgsplat::vulkan
