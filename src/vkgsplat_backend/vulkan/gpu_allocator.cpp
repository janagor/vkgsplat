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
#include <memory>
#include <span>
#include <system_error>
#include <utility>

namespace vkgsplat::vulkan {

namespace {

  [[nodiscard]] auto MakeAllocatorError(std::string message) -> Error
  { return MakeError(std::errc::io_error, std::move(message)); }

  [[nodiscard]] auto MakeAllocatorError(vkexec::error const &err) -> Error
  { return MakeAllocatorError(err.message()); }

  [[nodiscard]] auto MirrorGpuBuffer(std::unique_ptr<vkexec::gpu_buffer> owned) -> Buffer
  {
    Buffer buffer;
    buffer.handle = owned->handle();
    buffer.size = owned->size();
    buffer.allocation = VK_NULL_HANDLE;
    buffer.vkexec_buffer = std::move(owned);
    return buffer;
  }

  [[nodiscard]] auto MirrorHeapBuffer(std::unique_ptr<vkexec::descriptor_heap_buffer> owned) -> Buffer
  {
    Buffer buffer;
    buffer.handle = owned->handle();
    buffer.size = owned->size();
    buffer.allocation = VK_NULL_HANDLE;
    buffer.vkexec_heap_buffer = std::move(owned);
    return buffer;
  }

}// namespace

GPUAllocator::~GPUAllocator() noexcept
{
  if (allocator_ != VK_NULL_HANDLE) { vmaDestroyAllocator(std::exchange(allocator_, VK_NULL_HANDLE)); }
}

GPUAllocator::GPUAllocator(GPUAllocator &&other) noexcept
  : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)), device_(std::exchange(other.device_, VK_NULL_HANDLE)),
    get_buffer_device_address_(std::exchange(other.get_buffer_device_address_, nullptr)),
    vkexec_(std::exchange(other.vkexec_, nullptr))
{}

auto GPUAllocator::operator=(GPUAllocator &&other) noexcept -> GPUAllocator &
{
  if (this != &other) {
    if (allocator_ != VK_NULL_HANDLE) { vmaDestroyAllocator(std::exchange(allocator_, VK_NULL_HANDLE)); }
    allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    get_buffer_device_address_ = std::exchange(other.get_buffer_device_address_, nullptr);
    vkexec_ = std::exchange(other.vkexec_, nullptr);
  }
  return *this;
}

auto GPUAllocator::create(VkInstance instance, VkDevice device, VkPhysicalDevice physical_device) noexcept
  -> std::expected<GPUAllocator, Error>
{
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  auto const get_buffer_device_address_fn =
    reinterpret_cast<PFN_vkGetBufferDeviceAddress>(vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddress"));
  if (get_buffer_device_address_fn == nullptr) {
    return std::unexpected(MakeAllocatorError("vkGetBufferDeviceAddress unavailable"));
  }
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  VmaAllocatorCreateInfo info = {};
  info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  info.physicalDevice = physical_device;
  info.device = device;
  info.instance = instance;
  info.vulkanApiVersion = VK_API_VERSION_1_4;

  VmaAllocator allocator = nullptr;
  VkResult const result = vmaCreateAllocator(&info, &allocator);

  if (result != VK_SUCCESS) { return std::unexpected(MakeAllocatorError("Failed to create VMA allocator")); }

  return GPUAllocator{ allocator, device, get_buffer_device_address_fn };
}


GPUAllocator::GPUAllocator(VmaAllocator allocator,
  VkDevice device,
  PFN_vkGetBufferDeviceAddress get_buffer_device_address) noexcept
  : allocator_{ allocator }, device_{ device }, get_buffer_device_address_{ get_buffer_device_address }
{}

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
  return MirrorGpuBuffer(std::make_unique<vkexec::gpu_buffer>(std::move(*created)));
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
  return MirrorHeapBuffer(std::make_unique<vkexec::descriptor_heap_buffer>(std::move(*created)));
}

auto GPUAllocator::get_buffer_device_address(Buffer const &buffer) const noexcept -> VkDeviceAddress
{
  if (buffer.vkexec_buffer) {
    auto const address = buffer.vkexec_buffer->device_address();
    return address ? *address : 0;
  }
  if (buffer.vkexec_heap_buffer) {
    auto const address = buffer.vkexec_heap_buffer->device_address();
    return address ? *address : 0;
  }
  VkBufferDeviceAddressInfo const info{
    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    .pNext = nullptr,
    .buffer = buffer.handle,
  };
  return get_buffer_device_address_(device_, &info);
}

void GPUAllocator::destroy_buffer(Buffer &buffer) noexcept
{
  if (buffer.vkexec_buffer || buffer.vkexec_heap_buffer) {
    buffer = {};
    return;
  }
  if (buffer.handle != VK_NULL_HANDLE || buffer.allocation != VK_NULL_HANDLE) {
    vmaDestroyBuffer(allocator_, buffer.handle, buffer.allocation);
    buffer = {};
  }
}

auto GPUAllocator::map_buffer(Buffer const &buffer) noexcept -> std::expected<std::span<std::byte>, Error>
{
  if (buffer.vkexec_buffer) { return buffer.vkexec_buffer->mapped(); }
  if (buffer.vkexec_heap_buffer) { return buffer.vkexec_heap_buffer->mapped(); }

  void *data = nullptr;
  if (vmaMapMemory(allocator_, buffer.allocation, &data) != VK_SUCCESS) {
    return std::unexpected(MakeAllocatorError("Failed to map buffer"));
  }
  return std::span<std::byte>(static_cast<std::byte *>(data), buffer.size);
}

void GPUAllocator::unmap_buffer(Buffer const &buffer) noexcept
{
  if (buffer.vkexec_buffer || buffer.vkexec_heap_buffer) { return; }
  vmaUnmapMemory(allocator_, buffer.allocation);
}

void GPUAllocator::flush_buffer(Buffer const &buffer) noexcept
{
  if (buffer.vkexec_buffer || buffer.vkexec_heap_buffer) { return; }
  vmaFlushAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE);
}

void GPUAllocator::invalidate_buffer(Buffer const &buffer) noexcept
{
  if (buffer.vkexec_buffer || buffer.vkexec_heap_buffer) { return; }
  vmaInvalidateAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE);
}

}// namespace vkgsplat::vulkan
