#pragma once
#include <backend/vulkan/gpu_allocator.hpp>//NOLINT(misc-header-include-cycle)

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <type_traits>
#include <vector>

namespace vkgsplat::vulkan {

template<typename T>
  requires std::is_trivially_copyable_v<T>
auto GPUAllocator::write_buffer(Buffer const &buffer, std::span<const T> data) noexcept -> std::expected<void, void *>
{
  if (data.size_bytes() > buffer.size) { return std::unexpected(nullptr); }

  void *mapped = nullptr;
  if (vmaMapMemory(allocator_, buffer.allocation, &mapped) != VK_SUCCESS) { return std::unexpected(nullptr); }

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
  if (vmaMapMemory(allocator_, buffer.allocation, &mapped) != VK_SUCCESS) { return std::unexpected(nullptr); }

  std::vector<T> result(count);
  std::memcpy(result.data(), mapped, count * sizeof(T));
  vmaUnmapMemory(allocator_, buffer.allocation);

  return result;
}

}// namespace vkgsplat::vulkan
