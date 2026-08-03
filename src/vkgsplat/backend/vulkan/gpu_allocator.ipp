#pragma once
#include <backend/vulkan/gpu_allocator.hpp>// NOLINT(misc-header-include-cycle)

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <system_error>
#include <vector>

namespace vkgsplat::vulkan {

template<TriviallyCopyable T>
auto GPUAllocator::write_buffer(Buffer const &buffer, std::span<const T> data) noexcept -> std::expected<void, Error>
{
  if (data.size_bytes() > buffer.size) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "write_buffer: data exceeds buffer size"));
  }

  void *mapped = nullptr;
  if (vmaMapMemory(allocator_, buffer.allocation, &mapped) != VK_SUCCESS) {
    return std::unexpected(MakeError(std::errc::io_error, "write_buffer: failed to map memory"));
  }

  std::memcpy(mapped, data.data(), data.size_bytes());
  vmaUnmapMemory(allocator_, buffer.allocation);
  flush_buffer(buffer);

  return {};
}

template<TriviallyCopyable T>
auto GPUAllocator::read_buffer(Buffer const &buffer, std::size_t count) noexcept -> std::expected<std::vector<T>, Error>
{
  if (count * sizeof(T) > buffer.size) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "read_buffer: count exceeds buffer size"));
  }

  invalidate_buffer(buffer);

  void *mapped = nullptr;
  if (vmaMapMemory(allocator_, buffer.allocation, &mapped) != VK_SUCCESS) {
    return std::unexpected(MakeError(std::errc::io_error, "read_buffer: failed to map memory"));
  }

  std::vector<T> result(count);
  std::memcpy(result.data(), mapped, count * sizeof(T));
  vmaUnmapMemory(allocator_, buffer.allocation);

  return result;
}

}// namespace vkgsplat::vulkan
