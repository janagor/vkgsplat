#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_HPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_HPP

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vk_mem_alloc.h>
#include <vkexec/gpu_buffer.hpp>
#include <vkexec_extensions/descriptor_heap/buffer.hpp>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <system_error>
#include <variant>
#include <vector>

namespace vkexec {
class context;
}

namespace vkgsplat::vulkan {

/// Owning GPU buffer wrapper: exactly one vkexec buffer type, or empty.
struct Buffer
{
  std::variant<std::monostate, vkexec::gpu_buffer, vkexec::descriptor_heap_buffer> storage;

  Buffer() noexcept = default;
  ~Buffer() = default;
  Buffer(Buffer &&) noexcept = default;
  auto operator=(Buffer &&) noexcept -> Buffer & = default;
  Buffer(Buffer const &) = delete;
  auto operator=(Buffer const &) -> Buffer & = delete;

  [[nodiscard]] auto empty() const noexcept -> bool { return std::holds_alternative<std::monostate>(storage); }

  [[nodiscard]] auto handle() const noexcept -> VkBuffer;
  [[nodiscard]] auto size() const noexcept -> VkDeviceSize;
};

class [[nodiscard]] GPUAllocator
{
public:
  GPUAllocator() noexcept = default;
  ~GPUAllocator() noexcept;
  GPUAllocator(GPUAllocator const &) = delete;
  auto operator=(GPUAllocator const &) -> GPUAllocator & = delete;

  GPUAllocator(GPUAllocator &&other) noexcept;
  auto operator=(GPUAllocator &&other) noexcept -> GPUAllocator &;

  [[nodiscard]] static auto create(VkInstance instance, VkDevice device, VkPhysicalDevice physical_device) noexcept
    -> std::expected<GPUAllocator, Error>;

  /// Route buffer creates through the adopted vkexec context (same VMA).
  void bind_vkexec(vkexec::context &ctx) noexcept { vkexec_ = &ctx; }

  [[nodiscard]] auto create_storage_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>;
  // Device-local storage suitable for GPU write + transfer (e.g. color targets).
  [[nodiscard]] auto create_device_storage_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>;
  // Host-visible staging buffer for GPU→CPU readback (TRANSFER_DST).
  [[nodiscard]] auto create_staging_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>;
  [[nodiscard]] auto create_heap_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>;
  [[nodiscard]] auto vma_allocator() const noexcept -> VmaAllocator { return allocator_; }
  [[nodiscard]] static auto get_buffer_device_address(Buffer const &buffer) noexcept -> VkDeviceAddress;
  static void destroy_buffer(Buffer &buffer) noexcept;
  [[nodiscard]] static auto map_buffer(Buffer const &buffer) noexcept -> std::expected<std::span<std::byte>, Error>;

  template<TriviallyCopyable T>
  [[nodiscard]] static auto write_buffer(Buffer const &buffer, std::span<const T> data) noexcept
    -> std::expected<void, Error>;

  template<TriviallyCopyable T>
  [[nodiscard]] static auto read_buffer(Buffer const &buffer, std::size_t count) noexcept
    -> std::expected<std::vector<T>, Error>;

private:
  explicit GPUAllocator(VmaAllocator allocator) noexcept;

  [[nodiscard]] auto create_vkexec_buffer(VkDeviceSize size,
    vkexec::gpu_buffer_memory memory,
    bool shader_device_address) noexcept -> std::expected<Buffer, Error>;

  VmaAllocator allocator_{ VK_NULL_HANDLE };
  vkexec::context *vkexec_{ nullptr };
};

namespace gpu_allocator_detail {

  template<typename T>
  [[nodiscard]] inline auto CopyFromMapped(std::span<std::byte> mapped, std::span<const T> data)
    -> std::expected<void, Error>
  {
    if (mapped.size() < data.size_bytes()) {
      return std::unexpected(MakeError(std::errc::io_error, "write_buffer: buffer is not host-mapped"));
    }
    std::memcpy(mapped.data(), data.data(), data.size_bytes());
    return {};
  }

  template<typename T>
  [[nodiscard]] inline auto CopyToVector(std::span<std::byte> mapped, std::size_t count)
    -> std::expected<std::vector<T>, Error>
  {
    auto const bytes = count * sizeof(T);
    if (mapped.size() < bytes) {
      return std::unexpected(MakeError(std::errc::io_error, "read_buffer: buffer is not host-mapped"));
    }
    std::vector<T> result(count);
    std::memcpy(result.data(), mapped.data(), bytes);
    return result;
  }

}// namespace gpu_allocator_detail

template<TriviallyCopyable T>
auto GPUAllocator::write_buffer(Buffer const &buffer, std::span<const T> data) noexcept -> std::expected<void, Error>
{
  if (data.size_bytes() > buffer.size()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "write_buffer: data exceeds buffer size"));
  }

  if (auto const *gpu = std::get_if<vkexec::gpu_buffer>(&buffer.storage)) {
    return gpu_allocator_detail::CopyFromMapped(gpu->mapped(), data);
  }
  if (auto const *heap = std::get_if<vkexec::descriptor_heap_buffer>(&buffer.storage)) {
    return gpu_allocator_detail::CopyFromMapped(heap->mapped(), data);
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
    return gpu_allocator_detail::CopyToVector<T>(gpu->mapped(), count);
  }
  if (auto const *heap = std::get_if<vkexec::descriptor_heap_buffer>(&buffer.storage)) {
    return gpu_allocator_detail::CopyToVector<T>(heap->mapped(), count);
  }
  return std::unexpected(MakeError(std::errc::io_error, "read_buffer: empty buffer"));
}

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_HPP
