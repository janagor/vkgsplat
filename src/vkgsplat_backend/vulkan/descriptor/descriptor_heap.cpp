#include "vulkan/descriptor/descriptor_heap.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <utility>
#include <vector>

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

auto WriteStorageBufferDescriptor(vulkan::Context &context,
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

  return context.write_resource_descriptors(context.device, 1, &resource_info, &host_range) == VK_SUCCESS;
}

auto WriteStorageImageDescriptor(vulkan::Context &context,
  VkImageViewCreateInfo const &view_info,
  VkImageLayout layout,
  std::span<std::byte> destination) -> bool
{
  VkImageDescriptorInfoEXT image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
  image_info.pView = &view_info;
  image_info.layout = layout;

  VkResourceDescriptorInfoEXT resource_info{};
  resource_info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
  resource_info.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  resource_info.data.pImage = &image_info;

  VkHostAddressRangeEXT const host_range = { .address = destination.data(), .size = destination.size() };

  return context.write_resource_descriptors(context.device, 1, &resource_info, &host_range) == VK_SUCCESS;
}

auto QueryDescriptorHeapLayout(vulkan::Context const &context, RenderData &data) -> bool
{
  VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
  heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

  VkPhysicalDeviceProperties2 props2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &heap_props,
    .properties = context.device.physical_device.properties,
  };
  context.inst_disp.getPhysicalDeviceProperties2(context.device.physical_device, &props2);

  // Mix storage buffers and a storage image in one heap; stride must fit both.
  auto const descriptor_size =
    static_cast<size_t>(std::max(heap_props.bufferDescriptorSize, heap_props.imageDescriptorSize));
  auto const descriptor_alignment = std::max(heap_props.bufferDescriptorAlignment, heap_props.imageDescriptorAlignment);
  if (descriptor_size == 0 || (descriptor_alignment != 0 && descriptor_size % descriptor_alignment != 0)) {
    return false;
  }
  data.buffer_descriptor_size = static_cast<size_t>(heap_props.bufferDescriptorSize);
  data.image_descriptor_size = static_cast<size_t>(heap_props.imageDescriptorSize);
  data.descriptor_stride = descriptor_size;
  auto const descriptor_region_size = data.descriptor_stride * kHeapDescriptorCount;
  data.reserved_range_offset = AlignUp(descriptor_region_size, heap_props.resourceHeapAlignment);
  data.reserved_range_size = heap_props.minResourceHeapReservedRange;
  data.descriptor_heap_size = data.reserved_range_offset + data.reserved_range_size;

  return descriptor_size > 0;
}

void DestroyDescriptorHeap(vulkan::Context &context, RenderData &data)
{
  context.gpu_allocator.destroy_buffer(data.descriptor_heap_buffer);
  data.descriptor_heap_buffer = {};
}

auto RefreshDescriptorHeap(vulkan::Context &context, RenderData &data) -> bool
{
  if (!QueryDescriptorHeapLayout(context, data)) { return false; }

  DestroyDescriptorHeap(context, data);

  if (data.geometry_buffer.handle == VK_NULL_HANDLE || data.appearance_buffer.handle == VK_NULL_HANDLE
      || data.projected_buffer.handle == VK_NULL_HANDLE || data.unsorted_keys_buffer.handle == VK_NULL_HANDLE
      || data.unsorted_values_buffer.handle == VK_NULL_HANDLE || data.sorted_keys_buffer.handle == VK_NULL_HANDLE
      || data.sorted_values_buffer.handle == VK_NULL_HANDLE || data.sort_histogram_buffer.handle == VK_NULL_HANDLE
      || data.tile_ranges_buffer.handle == VK_NULL_HANDLE || data.sorted_indices.buffer().handle == VK_NULL_HANDLE
      || data.sort_entries.buffer().handle == VK_NULL_HANDLE) {
    return true;
  }

  auto descriptor_heap_buffer = context.gpu_allocator.create_heap_buffer(data.descriptor_heap_size);
  if (!descriptor_heap_buffer) {
    std::println("Failed to create descriptor heap buffer!");
    return false;
  }

  auto const descriptor_size = data.descriptor_stride;
  auto const geometry_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianGeometry));
  auto const appearance_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianAppearance));
  auto const projected_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianProjected));
  auto const unsorted_keys_buffer_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(gs::BinningKey));
  auto const unsorted_values_buffer_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(u32));
  auto const sorted_keys_buffer_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(gs::BinningKey));
  auto const sorted_values_buffer_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(u32));
  auto const sort_histogram_buffer_size =
    static_cast<VkDeviceSize>(static_cast<size_t>(data.radix_num_workgroups) * 256U * sizeof(u32));
  auto const tile_ranges_buffer_size = static_cast<VkDeviceSize>(data.tile_count * sizeof(gs::TileRange));
  auto const sorted_indices_buffer_size = data.sorted_indices.byte_size();
  auto const sort_entries_buffer_size = data.sort_entries.byte_size();

  std::vector<std::byte> descriptor_data(data.descriptor_stride * kHeapDescriptorCount);
  std::array<VkDeviceAddressRangeEXT, kHeapDescriptorCount> address_ranges{};
  address_ranges.at(static_cast<size_t>(HeapSlot::kGeometry)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.geometry_buffer),
    .size = geometry_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kAppearance)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.appearance_buffer),
    .size = appearance_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortedIndices)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sorted_indices.buffer()),
    .size = sorted_indices_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortEntries)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sort_entries.buffer()),
    .size = sort_entries_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kProjected)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.projected_buffer),
    .size = projected_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kUnsortedKeys)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.unsorted_keys_buffer),
    .size = unsorted_keys_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kUnsortedValues)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.unsorted_values_buffer),
    .size = unsorted_values_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortedKeys)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sorted_keys_buffer),
    .size = sorted_keys_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortedValues)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sorted_values_buffer),
    .size = sorted_values_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kTileRanges)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.tile_ranges_buffer),
    .size = tile_ranges_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortHistogram)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sort_histogram_buffer),
    .size = sort_histogram_buffer_size,
  };

  for (size_t i = 0; i < kHeapDescriptorCount; ++i) {
    auto const slot = static_cast<HeapSlot>(i);
    auto destination = std::span{ descriptor_data }.subspan(i * data.descriptor_stride, descriptor_size);

    if (slot == HeapSlot::kColorTarget) {
      // HW path: float color attachment + blit; not a storage image.
      continue;
    }

    if (!WriteStorageBufferDescriptor(context,
          address_ranges.at(i).address,
          address_ranges.at(i).size,
          destination.first(data.buffer_descriptor_size))) {
      std::println("Failed to write descriptor heap slot {}!", i);
      return false;
    }
  }

  if (!context.gpu_allocator.write_buffer<std::byte>(*descriptor_heap_buffer, std::span{ descriptor_data })) {
    std::println("Failed to upload descriptor heap!");
    return false;
  }

  data.descriptor_heap_buffer = std::move(*descriptor_heap_buffer);
  return true;
}

void BindDescriptorHeap(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
{
  VkDeviceAddress const heap_address = context.gpu_allocator.get_buffer_device_address(data.descriptor_heap_buffer);
  VkBindHeapInfoEXT const bind_heap_info = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = { .address = heap_address, .size = data.descriptor_heap_size },
    .reservedRangeOffset = data.reserved_range_offset,
    .reservedRangeSize = data.reserved_range_size,
  };
  context.cmd_bind_resource_heap(command_buffer, &bind_heap_info);
}

auto HeapSlotByteOffset(RenderData const &data, HeapSlot slot) -> uint32_t
{ return static_cast<uint32_t>(static_cast<size_t>(slot) * data.descriptor_stride); }

}// namespace vkgsplat
