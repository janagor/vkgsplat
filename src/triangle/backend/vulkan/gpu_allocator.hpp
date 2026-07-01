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
  explicit GPUAllocator(VmaAllocator allocator, VkDevice device, PFN_vkGetBufferDeviceAddress get_buffer_device_address) noexcept;

  VmaAllocator allocator_{ VK_NULL_HANDLE };
  VkDevice device_{ VK_NULL_HANDLE };
  PFN_vkGetBufferDeviceAddress get_buffer_device_address_{ nullptr };
};

template<typename T>
  requires std::is_trivially_copyable_v<T>
auto GPUAllocator::write_buffer(Buffer const &buffer, std::span<const T> data) noexcept -> std::expected<void, void *>
{
  if (data.size_bytes() > buffer.size) { return std::unexpected(nullptr); }

  void *mapped = nullptr;
  if (vmaMapMemory(allocator_, buffer.allocation, &mapped) != VK_SUCCESS) {
    return std::unexpected(nullptr);
  }

  std::memcpy(mapped, data.data(), data.size_bytes());
  vmaUnmapMemory(allocator_, buffer.allocation);
  flush_buffer(buffer);

  return {};
}

template<typename T>
  requires std::is_trivially_copyable_v<T>
auto GPUAllocator::read_buffer(Buffer const &buffer, std::size_t count) noexcept
  -> std::expected<std::vector<T>, void *>
{
  if (count * sizeof(T) > buffer.size) { return std::unexpected(nullptr); }

  invalidate_buffer(buffer);

  void *mapped = nullptr;
  if (vmaMapMemory(allocator_, buffer.allocation, &mapped) != VK_SUCCESS) {
    return std::unexpected(nullptr);
  }

  std::vector<T> result(count);
  std::memcpy(result.data(), mapped, count * sizeof(T));
  vmaUnmapMemory(allocator_, buffer.allocation);

  return result;
}

}// namespace vkgsplat::vulkan
