#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <type_traits>
#include <vector>

namespace vkgsplat::vulkan {

struct Buffer
{
  VkBuffer handle = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
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
  auto create_heap_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, void *>;
  auto get_buffer_device_address(Buffer const &buffer) const noexcept -> VkDeviceAddress;
  void destroy_buffer(Buffer &buffer) noexcept;
  auto map_buffer(Buffer const &buffer) noexcept -> std::expected<std::span<std::byte>, void *>;
  void unmap_buffer(Buffer const &buffer) noexcept;
  void flush_buffer(Buffer const &buffer) noexcept;
  void invalidate_buffer(Buffer const &buffer) noexcept;

  template<typename T>
    requires std::is_trivially_copyable_v<T>
  auto write_buffer(Buffer const &buffer, std::span<const T> data) noexcept -> std::expected<void, void *>;

  template<typename T>
    requires std::is_trivially_copyable_v<T>
  auto read_buffer(Buffer const &buffer, std::size_t count) noexcept -> std::expected<std::vector<T>, void *>;

private:
  explicit GPUAllocator(VmaAllocator allocator,
    VkDevice device,
    PFN_vkGetBufferDeviceAddress get_buffer_device_address) noexcept;

  VmaAllocator allocator_{ VK_NULL_HANDLE };
  VkDevice device_{ VK_NULL_HANDLE };
  PFN_vkGetBufferDeviceAddress get_buffer_device_address_{ nullptr };
};


}// namespace vkgsplat::vulkan
#include <backend/vulkan/gpu_allocator.ipp>
