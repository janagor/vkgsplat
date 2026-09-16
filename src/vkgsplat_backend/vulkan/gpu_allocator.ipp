#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_IPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_IPP

#include <vulkan/gpu_allocator.hpp>// NOLINT(misc-header-include-cycle)

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <system_error>
#include <variant>
#include <vector>

namespace vkgsplat::vulkan {

namespace {

  template<typename T>
  [[nodiscard]] auto CopyFromMapped(std::span<std::byte> mapped, std::span<const T> data) -> std::expected<void, Error>
  {
    if (mapped.size() < data.size_bytes()) {
      return std::unexpected(MakeError(std::errc::io_error, "write_buffer: buffer is not host-mapped"));
    }
    std::memcpy(mapped.data(), data.data(), data.size_bytes());
    return {};
  }

  template<typename T>
  [[nodiscard]] auto CopyToVector(std::span<std::byte> mapped, std::size_t count) -> std::expected<std::vector<T>, Error>
  {
    auto const bytes = count * sizeof(T);
    if (mapped.size() < bytes) {
      return std::unexpected(MakeError(std::errc::io_error, "read_buffer: buffer is not host-mapped"));
    }
    std::vector<T> result(count);
    std::memcpy(result.data(), mapped.data(), bytes);
    return result;
  }

}// namespace

template<TriviallyCopyable T>
auto GPUAllocator::write_buffer(Buffer const &buffer, std::span<const T> data) noexcept -> std::expected<void, Error>
{
  if (data.size_bytes() > buffer.size()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "write_buffer: data exceeds buffer size"));
  }

  if (auto const *gpu = std::get_if<vkexec::gpu_buffer>(&buffer.storage)) {
    return CopyFromMapped(gpu->mapped(), data);
  }
  if (auto const *heap = std::get_if<vkexec::descriptor_heap_buffer>(&buffer.storage)) {
    return CopyFromMapped(heap->mapped(), data);
  }
  return std::unexpected(MakeError(std::errc::io_error, "write_buffer: empty buffer"));
}

template<TriviallyCopyable T>
auto GPUAllocator::read_buffer(Buffer const &buffer, std::size_t count) noexcept -> std::expected<std::vector<T>, Error>
{
  if (count * sizeof(T) > buffer.size()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "read_buffer: count exceeds buffer size"));
  }

  if (auto const *gpu = std::get_if<vkexec::gpu_buffer>(&buffer.storage)) {
    return CopyToVector<T>(gpu->mapped(), count);
  }
  if (auto const *heap = std::get_if<vkexec::descriptor_heap_buffer>(&buffer.storage)) {
    return CopyToVector<T>(heap->mapped(), count);
  }
  return std::unexpected(MakeError(std::errc::io_error, "read_buffer: empty buffer"));
}

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_IPP
