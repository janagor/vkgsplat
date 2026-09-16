#include <string>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>// NOLINT
#include <vulkan/vulkan_core.h>

#include <vulkan/gpu_allocator.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vkexec/error.hpp>
#include <vkexec/gpu_buffer.hpp>
#include <vkexec/sync_wait.hpp>
#include <vkexec_extensions/descriptor_heap/buffer.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <system_error>
#include <utility>
#include <variant>

namespace vkgsplat::vulkan {

namespace {

  [[nodiscard]] auto MakeAllocatorError(std::string message) -> Error
  { return MakeError(std::errc::io_error, std::move(message)); }

  [[nodiscard]] auto MakeAllocatorError(vkexec::error const &err) -> Error
  { return MakeAllocatorError(err.message()); }

}// namespace

GPUAllocator::~GPUAllocator() noexcept
{
  if (allocator_ != VK_NULL_HANDLE) { vmaDestroyAllocator(std::exchange(allocator_, VK_NULL_HANDLE)); }
}

GPUAllocator::GPUAllocator(GPUAllocator &&other) noexcept
  : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)), vkexec_(std::exchange(other.vkexec_, nullptr))
{}

auto GPUAllocator::operator=(GPUAllocator &&other) noexcept -> GPUAllocator &
{
  if (this != &other) {
    if (allocator_ != VK_NULL_HANDLE) { vmaDestroyAllocator(std::exchange(allocator_, VK_NULL_HANDLE)); }
    allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
    vkexec_ = std::exchange(other.vkexec_, nullptr);
  }
  return *this;
}

auto GPUAllocator::create(VkInstance instance, VkDevice device, VkPhysicalDevice physical_device) noexcept
  -> std::expected<GPUAllocator, Error>
{
  VmaAllocatorCreateInfo info = {};
  info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  info.physicalDevice = physical_device;
  info.device = device;
  info.instance = instance;
  info.vulkanApiVersion = VK_API_VERSION_1_4;

  VmaAllocator allocator = nullptr;
  VkResult const result = vmaCreateAllocator(&info, &allocator);

  if (result != VK_SUCCESS) { return std::unexpected(MakeAllocatorError("Failed to create VMA allocator")); }

  return GPUAllocator{ allocator };
}

GPUAllocator::GPUAllocator(VmaAllocator allocator) noexcept : allocator_{ allocator } {}

auto GPUAllocator::create_vkexec_buffer(VkDeviceSize size,
  vkexec::gpu_buffer_memory memory,
  bool shader_device_address) noexcept -> std::expected<Buffer, Error>
{
  if (vkexec_ == nullptr) {
    return std::unexpected(MakeAllocatorError("vkexec context not bound; call bind_vkexec after adopt"));
  }

  auto created = vkexec::try_sync_wait_value(vkexec::gpu_buffer::create(*vkexec_,
    vkexec::gpu_buffer_create_info{
      .size = size,
      .memory = memory,
      .shader_device_address = shader_device_address,
    }));
  if (!created) { return std::unexpected(MakeAllocatorError(created.error())); }

  Buffer buffer;
  buffer.storage = std::move(*created);
  return buffer;
}

auto GPUAllocator::create_storage_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>
{
  return create_vkexec_buffer(size, vkexec::gpu_buffer_memory::host_visible, true);
}

auto GPUAllocator::create_device_storage_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>
{
  return create_vkexec_buffer(size, vkexec::gpu_buffer_memory::device_local, true);
}

auto GPUAllocator::create_staging_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>
{
  return create_vkexec_buffer(size, vkexec::gpu_buffer_memory::staging, false);
}

auto GPUAllocator::create_heap_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>
{
  if (vkexec_ == nullptr) {
    return std::unexpected(MakeAllocatorError("vkexec context not bound; call bind_vkexec after adopt"));
  }

  auto created = vkexec::try_sync_wait_value(vkexec::descriptor_heap_buffer::create(*vkexec_, size));
  if (!created) { return std::unexpected(MakeAllocatorError(created.error())); }

  Buffer buffer;
  buffer.storage = std::move(*created);
  return buffer;
}

auto GPUAllocator::get_buffer_device_address(Buffer const &buffer) noexcept -> VkDeviceAddress
{
  if (auto const *gpu = std::get_if<vkexec::gpu_buffer>(&buffer.storage)) {
    auto const address = gpu->device_address();
    return address ? *address : 0;
  }
  if (auto const *heap = std::get_if<vkexec::descriptor_heap_buffer>(&buffer.storage)) {
    auto const address = heap->device_address();
    return address ? *address : 0;
  }
  return 0;
}

void GPUAllocator::destroy_buffer(Buffer &buffer) noexcept { buffer = {}; }

auto GPUAllocator::map_buffer(Buffer const &buffer) noexcept -> std::expected<std::span<std::byte>, Error>
{
  if (auto const *gpu = std::get_if<vkexec::gpu_buffer>(&buffer.storage)) { return gpu->mapped(); }
  if (auto const *heap = std::get_if<vkexec::descriptor_heap_buffer>(&buffer.storage)) { return heap->mapped(); }
  return std::unexpected(MakeAllocatorError("Cannot map empty buffer"));
}

void GPUAllocator::unmap_buffer(Buffer const &buffer) noexcept
{
  // vkexec buffers are persistently mapped; nothing to unmap.
  (void)buffer;
}

void GPUAllocator::flush_buffer(Buffer const &buffer) noexcept
{
  // vkexec host-visible buffers do not require explicit flush via VMA.
  (void)buffer;
}

void GPUAllocator::invalidate_buffer(Buffer const &buffer) noexcept
{
  // vkexec host-visible buffers do not require explicit invalidate via VMA.
  (void)buffer;
}

}// namespace vkgsplat::vulkan
