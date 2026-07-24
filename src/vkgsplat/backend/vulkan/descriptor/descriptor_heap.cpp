#include "backend/vulkan/descriptor/descriptor_heap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <vector>

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

using namespace gs;

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

auto query_descriptor_heap_layout(Init const &init, RenderData &data) -> bool
{
  VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
  heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

  VkPhysicalDeviceProperties2 props2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &heap_props,
    .properties = init.device.physical_device.properties,
  };
  init.inst_disp.getPhysicalDeviceProperties2(init.device.physical_device, &props2);

  auto const descriptor_size = static_cast<size_t>(heap_props.bufferDescriptorSize);
  if (descriptor_size == 0
      || (heap_props.bufferDescriptorAlignment != 0
          && heap_props.bufferDescriptorSize % heap_props.bufferDescriptorAlignment != 0)) {
    return false;
  }
  data.descriptor_stride = descriptor_size;
  auto const descriptor_region_size = data.descriptor_stride * k_heap_descriptor_count;
  data.reserved_range_offset = align_up(descriptor_region_size, heap_props.resourceHeapAlignment);
  data.reserved_range_size = heap_props.minResourceHeapReservedRange;
  data.descriptor_heap_size = data.reserved_range_offset + data.reserved_range_size;

  return descriptor_size > 0;
}

void destroy_descriptor_heap(Init &init, RenderData &data)
{
  init.gpu_allocator.destroy_buffer(data.descriptor_heap_buffer);
  data.descriptor_heap_buffer = {};
}

auto refresh_descriptor_heap(Init &init, RenderData &data) -> bool
{
  if (!query_descriptor_heap_layout(init, data)) { return false; }

  destroy_descriptor_heap(init, data);

  if (data.geometry_buffer.handle == VK_NULL_HANDLE || data.appearance_buffer.handle == VK_NULL_HANDLE
      || data.projected_buffer.handle == VK_NULL_HANDLE || data.unsorted_keys_buffer.handle == VK_NULL_HANDLE
      || data.unsorted_values_buffer.handle == VK_NULL_HANDLE || data.sorted_keys_buffer.handle == VK_NULL_HANDLE
      || data.sorted_values_buffer.handle == VK_NULL_HANDLE || data.sort_histogram_buffer.handle == VK_NULL_HANDLE
      || data.tile_ranges_buffer.handle == VK_NULL_HANDLE || data.color_buffer.handle == VK_NULL_HANDLE
      || data.sorted_indices.buffer().handle == VK_NULL_HANDLE || data.sort_entries.buffer().handle == VK_NULL_HANDLE) {
    return true;
  }

  auto descriptor_heap_buffer = init.gpu_allocator.create_heap_buffer(data.descriptor_heap_size);
  if (!descriptor_heap_buffer) {
    std::println("Failed to create descriptor heap buffer!");
    return false;
  }

  auto const descriptor_size = data.descriptor_stride;
  auto const geometry_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(GaussianGeometry));
  auto const appearance_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(GaussianAppearance));
  auto const projected_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(GaussianProjected));
  auto const unsorted_keys_buffer_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(BinningKey));
  auto const unsorted_values_buffer_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(u32));
  auto const sorted_keys_buffer_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(BinningKey));
  auto const sorted_values_buffer_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(u32));
  auto const sort_histogram_buffer_size = static_cast<VkDeviceSize>(
    static_cast<size_t>(data.radix_num_workgroups) * 256U * sizeof(u32));
  auto const tile_ranges_buffer_size = static_cast<VkDeviceSize>(data.tile_count * sizeof(TileRange));
  auto const color_buffer_size =
    static_cast<VkDeviceSize>(data.color_width) * static_cast<VkDeviceSize>(data.color_height) * 4U * sizeof(f32);
  auto const sorted_indices_buffer_size = data.sorted_indices.byte_size();
  auto const sort_entries_buffer_size = data.sort_entries.byte_size();

  std::vector<std::byte> descriptor_data(data.descriptor_stride * k_heap_descriptor_count);
  std::array<VkDeviceAddressRangeEXT, k_heap_descriptor_count> address_ranges = {
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.geometry_buffer),
      .size = geometry_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.appearance_buffer),
      .size = appearance_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.sorted_indices.buffer()),
      .size = sorted_indices_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.sort_entries.buffer()),
      .size = sort_entries_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.projected_buffer),
      .size = projected_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.unsorted_keys_buffer),
      .size = unsorted_keys_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.unsorted_values_buffer),
      .size = unsorted_values_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.sorted_keys_buffer),
      .size = sorted_keys_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.sorted_values_buffer),
      .size = sorted_values_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.tile_ranges_buffer),
      .size = tile_ranges_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.color_buffer),
      .size = color_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.sort_histogram_buffer),
      .size = sort_histogram_buffer_size,
    },
  };

  for (size_t i = 0; i < k_heap_descriptor_count; ++i) {
    if (!write_storage_buffer_descriptor(init,
          address_ranges.at(i).address,
          address_ranges.at(i).size,
          std::span{ descriptor_data }.subspan(i * data.descriptor_stride, descriptor_size))) {
      std::println("Failed to write descriptor heap slot {}!", i);
      return false;
    }
  }

  if (!init.gpu_allocator.write_buffer<std::byte>(*descriptor_heap_buffer, std::span{ descriptor_data })) {
    std::println("Failed to upload descriptor heap!");
    return false;
  }

  data.descriptor_heap_buffer = *descriptor_heap_buffer;
  return true;
}

void bind_descriptor_heap(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
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

auto heap_slot_byte_offset(RenderData const &data, HeapSlot slot) -> uint32_t
{ return static_cast<uint32_t>(static_cast<size_t>(slot) * data.descriptor_stride); }

}// namespace vkgsplat
