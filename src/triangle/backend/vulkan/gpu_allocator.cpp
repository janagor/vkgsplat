#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>// NOLINT
#include <vulkan/vulkan_core.h>

#include <backend/vulkan/gpu_allocator.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <utility>

namespace vkgsplat::vulkan {

GPUAllocator::~GPUAllocator() noexcept
{
  if (allocator_ != VK_NULL_HANDLE) { vmaDestroyAllocator(std::exchange(allocator_, VK_NULL_HANDLE)); }
}

GPUAllocator::GPUAllocator(GPUAllocator &&other) noexcept : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE))
{}

GPUAllocator &GPUAllocator::operator=(GPUAllocator &&other) noexcept
{
  if (this != &other) {
    if (allocator_ != VK_NULL_HANDLE) { vmaDestroyAllocator(allocator_); }
    allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
  }
  return *this;
}

// TODO: add correct error
std::expected<GPUAllocator, void *>
  GPUAllocator::create(VkInstance instance, VkDevice device, VkPhysicalDevice physical_device) noexcept
{
  VmaAllocatorCreateInfo info = {};
  // info.flags = 0;
  info.physicalDevice = physical_device;
  info.device = device;
  info.instance = instance;
  info.vulkanApiVersion = VK_API_VERSION_1_4;


  VmaAllocator allocator = nullptr;
  VkResult const result = vmaCreateAllocator(&info, &allocator);

  if (result != VK_SUCCESS) { return std::unexpected(nullptr); }

  return GPUAllocator{ allocator };
}


GPUAllocator::GPUAllocator(VmaAllocator allocator) noexcept : allocator_{ allocator } {}

std::expected<Buffer, void *> GPUAllocator::create_storage_buffer(VkDeviceSize size) noexcept
{
  VkBufferCreateInfo buffer_info = {};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_info = {};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
  alloc_info.flags =
    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

  Buffer buffer{ .size = size };
  if (vmaCreateBuffer(allocator_, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr)
      != VK_SUCCESS) {
    return std::unexpected(nullptr);
  }

  return buffer;
}

void GPUAllocator::destroy_buffer(Buffer &buffer) noexcept
{
  if (buffer.handle != VK_NULL_HANDLE || buffer.allocation != VK_NULL_HANDLE) {
    vmaDestroyBuffer(allocator_, buffer.handle, buffer.allocation);
    buffer = {};
  }
}

std::expected<std::span<std::byte>, void *> GPUAllocator::map_buffer(Buffer const &buffer) noexcept
{
  void *data = nullptr;
  if (vmaMapMemory(allocator_, buffer.allocation, &data) != VK_SUCCESS) { return std::unexpected(nullptr); }
  return std::span<std::byte>(static_cast<std::byte *>(data), buffer.size);
}

void GPUAllocator::unmap_buffer(Buffer const &buffer) noexcept { vmaUnmapMemory(allocator_, buffer.allocation); }

void GPUAllocator::flush_buffer(Buffer const &buffer) noexcept
{
  vmaFlushAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE);
}

void GPUAllocator::invalidate_buffer(Buffer const &buffer) noexcept
{
  vmaInvalidateAllocation(allocator_, buffer.allocation, 0, VK_WHOLE_SIZE);
}

}// namespace vkgsplat::vulkan
