#include "descriptor_heap.hpp"

#include <cstddef>
#include <span>

#include "app_state.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

auto align_up(VkDeviceSize value, VkDeviceSize alignment) -> VkDeviceSize
{ return (value + alignment - 1) / alignment * alignment; }

auto write_storage_buffer_descriptor(Init &init,
  VkDeviceAddress buffer_address,
  VkDeviceSize buffer_size,
  std::span<std::byte> destination) -> bool
{
  VkDeviceAddressRangeEXT const address_range = { .address = buffer_address, .size = buffer_size };
  VkResourceDescriptorInfoEXT resource_info{};
  resource_info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
  resource_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  resource_info.data.pAddressRange = &address_range;

  VkHostAddressRangeEXT const host_range = { .address = destination.data(), .size = destination.size() };

  return init.write_resource_descriptors(init.device, 1, &resource_info, &host_range) == VK_SUCCESS;
}

void bind_mesh_descriptor_heap(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  VkDeviceAddress const heap_address = init.gpu_allocator.get_buffer_device_address(data.descriptor_heap_buffer);
  VkBindHeapInfoEXT const bind_heap_info = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = { .address = heap_address, .size = data.descriptor_heap_size },
    .reservedRangeOffset = data.reserved_range_offset,
    .reservedRangeSize = data.reserved_range_size,
  };
  init.cmd_bind_resource_heap(command_buffer, &bind_heap_info);
}

}// namespace vkgsplat
