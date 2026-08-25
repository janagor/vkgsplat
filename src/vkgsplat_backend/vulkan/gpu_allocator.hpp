#ifndef VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_HPP
#define VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_HPP

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vk_mem_alloc.h>
#include <vkexec/gpu_buffer.hpp>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <vector>

namespace vkexec {
class context;
}

namespace vkgsplat::vulkan {

/// GPU buffer: vkexec-backed when allocated via `GPUAllocator` after `bind_vkexec`.
/// `handle` / `size` are mirrored for existing call sites.
struct Buffer
{
  std::unique_ptr<vkexec::gpu_buffer> vkexec_buffer;
  VkBuffer handle = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkDeviceSize size = 0;

  Buffer() noexcept = default;
  ~Buffer() = default;
  Buffer(Buffer &&) noexcept = default;
  auto operator=(Buffer &&) noexcept -> Buffer & = default;
  Buffer(Buffer const &) = delete;
  auto operator=(Buffer const &) -> Buffer & = delete;
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
  [[nodiscard]] auto get_buffer_device_address(Buffer const &buffer) const noexcept -> VkDeviceAddress;
  void destroy_buffer(Buffer &buffer) noexcept;
  [[nodiscard]] auto map_buffer(Buffer const &buffer) noexcept -> std::expected<std::span<std::byte>, Error>;
  void unmap_buffer(Buffer const &buffer) noexcept;
  void flush_buffer(Buffer const &buffer) noexcept;
  void invalidate_buffer(Buffer const &buffer) noexcept;

  template<TriviallyCopyable T>
  [[nodiscard]] auto write_buffer(Buffer const &buffer, std::span<const T> data) noexcept -> std::expected<void, Error>;

  template<TriviallyCopyable T>
  [[nodiscard]] auto read_buffer(Buffer const &buffer, std::size_t count) noexcept
    -> std::expected<std::vector<T>, Error>;

private:
  explicit GPUAllocator(VmaAllocator allocator,
    VkDevice device,
    PFN_vkGetBufferDeviceAddress get_buffer_device_address) noexcept;

  [[nodiscard]] auto create_vkexec_buffer(VkDeviceSize size,
    vkexec::gpu_buffer_memory memory,
    bool shader_device_address) noexcept -> std::expected<Buffer, Error>;

  VmaAllocator allocator_{ VK_NULL_HANDLE };
  VkDevice device_{ VK_NULL_HANDLE };
  PFN_vkGetBufferDeviceAddress get_buffer_device_address_{ nullptr };
  vkexec::context *vkexec_{ nullptr };
};

}// namespace vkgsplat::vulkan
#include <vulkan/gpu_allocator.ipp>

#endif// VKGSPLAT_BACKEND_VULKAN_GPU_ALLOCATOR_HPP
