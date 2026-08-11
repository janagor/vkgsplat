#include <string>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>// NOLINT
#include <vulkan/vulkan_core.h>

#include <vulkan/gpu_allocator.hpp>
#include <vulkan/initializers.hpp>
#include <vkgsplat_utility/error.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <system_error>
#include <utility>

namespace vkgsplat::vulkan {

namespace {

  [[nodiscard]] auto MakeAllocatorError(std::string message) -> Error
  { return MakeError(std::errc::io_error, std::move(message)); }

}// namespace

GPUAllocator::~GPUAllocator() noexcept
{
  if (allocator_ != VK_NULL_HANDLE) { vmaDestroyAllocator(std::exchange(allocator_, VK_NULL_HANDLE)); }
}

GPUAllocator::GPUAllocator(GPUAllocator &&other) noexcept
  : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)), device_(std::exchange(other.device_, VK_NULL_HANDLE)),
    get_buffer_device_address_(std::exchange(other.get_buffer_device_address_, nullptr))
{}

auto GPUAllocator::operator=(GPUAllocator &&other) noexcept -> GPUAllocator &
{
  if (this != &other) {
    if (allocator_ != VK_NULL_HANDLE) { vmaDestroyAllocator(std::exchange(allocator_, VK_NULL_HANDLE)); }
    allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    get_buffer_device_address_ = std::exchange(other.get_buffer_device_address_, nullptr);
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

auto GPUAllocator::create_storage_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>
{
  // NOLINTBEGIN(hicpp-signed-bitwise)
  auto const buffer_info = initializers::BufferCreateInfo(size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
  // NOLINTEND(hicpp-signed-bitwise)

  VmaAllocationCreateInfo alloc_info = {};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
  // Host uploads via write_buffer() are contiguous memcpy; VMA forbids combining
  // SEQUENTIAL_WRITE with RANDOM.
  alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

  Buffer buffer{ .size = size };
  if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr)
      != VK_SUCCESS) {
    return std::unexpected(MakeAllocatorError("Failed to create storage buffer"));
  }

  return buffer;
}

auto GPUAllocator::create_device_storage_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>
{
  // NOLINTBEGIN(hicpp-signed-bitwise)
  auto const buffer_info = initializers::BufferCreateInfo(size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
  // NOLINTEND(hicpp-signed-bitwise)

  VmaAllocationCreateInfo alloc_info = {};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  alloc_info.flags = 0;

  Buffer buffer{ .size = size };
  if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr)
      != VK_SUCCESS) {
    return std::unexpected(MakeAllocatorError("Failed to create device storage buffer"));
  }

  return buffer;
}

auto GPUAllocator::create_staging_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>
{
  auto const buffer_info = initializers::BufferCreateInfo(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

  VmaAllocationCreateInfo alloc_info = {};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
  // NOLINTBEGIN(hicpp-signed-bitwise)
  alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  // NOLINTEND(hicpp-signed-bitwise)
  alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  alloc_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  Buffer buffer{ .size = size };
  if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr)
      != VK_SUCCESS) {
    return std::unexpected(MakeAllocatorError("Failed to create staging buffer"));
  }

  return buffer;
}

auto GPUAllocator::create_heap_buffer(VkDeviceSize size) noexcept -> std::expected<Buffer, Error>
{
  auto const buffer_info = initializers::BufferCreateInfo(
    size, VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

  VmaAllocationCreateInfo alloc_info = {};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
  // NOLINTBEGIN(hicpp-signed-bitwise)
  // Persistently mapped descriptor heaps are written at arbitrary slot offsets.
  alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                     // ANV (Gfx < 12.5) bindless heap addressing assumes a 4 KiB-aligned
                     // device address; VMA suballocs of tiny heaps often are not.
                     | VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  // NOLINTEND(hicpp-signed-bitwise)
  alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  alloc_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  // Match ANV's BindlessSurfaceStateBaseAddress 4 KiB granularity.
  constexpr VkDeviceSize kHeapDeviceAddressAlignment = 4096;

  Buffer buffer{ .size = size };
  if (vmaCreateBufferWithAlignment(
        allocator_, &buffer_info, &alloc_info, kHeapDeviceAddressAlignment, &buffer.handle, &buffer.allocation, nullptr)
      != VK_SUCCESS) {
    return std::unexpected(MakeAllocatorError("Failed to create descriptor heap buffer"));
  }

  return buffer;
}

auto GPUAllocator::get_buffer_device_address(Buffer const &buffer) const noexcept -> VkDeviceAddress
{
  VkBufferDeviceAddressInfo const info{
    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    .pNext = nullptr,
    .buffer = buffer.handle,
  };
  return get_buffer_device_address_(device_, &info);
}

void GPUAllocator::destroy_buffer(Buffer &buffer) noexcept
{
  if (buffer.handle != VK_NULL_HANDLE || buffer.allocation != VK_NULL_HANDLE) {
    vmaDestroyBuffer(allocator_, buffer.handle, buffer.allocation);
    buffer = {};
  }
}

auto GPUAllocator::map_buffer(Buffer const &buffer) noexcept -> std::expected<std::span<std::byte>, Error>
{
  void *data = nullptr;
  if (vmaMapMemory(allocator_, buffer.allocation, &data) != VK_SUCCESS) {
    return std::unexpected(MakeAllocatorError("Failed to map buffer"));
  }
  return std::span<std::byte>(static_cast<std::byte *>(data), buffer.size);
}

void GPUAllocator::unmap_buffer(Buffer const &buffer) noexcept { vmaUnmapMemory(allocator_, buffer.allocation); }

void GPUAllocator::flush_buffer(Buffer const &buffer) noexcept
{ vmaFlushAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE); }

void GPUAllocator::invalidate_buffer(Buffer const &buffer) noexcept
{ vmaInvalidateAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE); }

}// namespace vkgsplat::vulkan
