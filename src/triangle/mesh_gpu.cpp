#include "mesh_gpu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <print>
#include <span>
#include <vector>

#include "app_state.hpp"
#include "descriptor_heap.hpp"
#include "types.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void destroy_mesh_buffers(Init &init, RenderData &data)
{
  init.gpu_allocator.destroy_buffer(data.position_buffer);
  init.gpu_allocator.destroy_buffer(data.color_buffer);
  init.gpu_allocator.destroy_buffer(data.descriptor_heap_buffer);
  data.position_buffer = {};
  data.color_buffer = {};
  data.descriptor_heap_buffer = {};
}

auto refresh_mesh_descriptor_heap(Init &init, RenderData &data) -> bool
{
  init.gpu_allocator.destroy_buffer(data.descriptor_heap_buffer);
  data.descriptor_heap_buffer = {};

  if (data.mesh.vertex_count() == 0 || data.sorted_indices_buffer.handle == VK_NULL_HANDLE) { return true; }

  VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
  heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

  VkPhysicalDeviceProperties2 props2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &heap_props,
    .properties = init.device.physical_device.properties,
  };
  init.inst_disp.getPhysicalDeviceProperties2(init.device.physical_device, &props2);

  auto const descriptor_size = static_cast<size_t>(heap_props.bufferDescriptorSize);
  data.descriptor_stride =
    static_cast<size_t>(align_up(heap_props.bufferDescriptorSize, heap_props.bufferDescriptorAlignment));
  VkDeviceSize const descriptor_region_size = data.descriptor_stride * 3;
  data.reserved_range_offset = align_up(descriptor_region_size, heap_props.resourceHeapAlignment);
  data.reserved_range_size = heap_props.minResourceHeapReservedRange;
  data.descriptor_heap_size = data.reserved_range_offset + data.reserved_range_size;

  auto descriptor_heap_buffer = init.gpu_allocator.create_heap_buffer(data.descriptor_heap_size);
  if (!descriptor_heap_buffer) {
    std::println("Failed to create mesh descriptor heap buffer!");
    return false;
  }

  auto const position_buffer_size = static_cast<VkDeviceSize>(
    data.mesh_buffer_vertex_capacity * sizeof(data.mesh.positions.front()));
  auto const color_buffer_size =
    static_cast<VkDeviceSize>(data.mesh_buffer_vertex_capacity * sizeof(data.mesh.colors.front()));
  auto const sorted_indices_buffer_size = static_cast<VkDeviceSize>(k_triangle_count * sizeof(u32));

  std::vector<std::byte> descriptor_data(data.descriptor_stride * 3);
  std::array<VkDeviceAddressRangeEXT, 3> address_ranges = {
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.position_buffer),
      .size = position_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.color_buffer),
      .size = color_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.sorted_indices_buffer),
      .size = sorted_indices_buffer_size,
    },
  };

  for (size_t i = 0; i < 3; ++i) {
    if (!write_storage_buffer_descriptor(init,
          address_ranges.at(i).address,
          address_ranges.at(i).size,
          std::span{ descriptor_data }.subspan(i * data.descriptor_stride, descriptor_size))) {
      std::println("Failed to write mesh buffer descriptor {}!", i);
      return false;
    }
  }

  if (!init.gpu_allocator.write_buffer<std::byte>(*descriptor_heap_buffer, std::span{ descriptor_data })) {
    std::println("Failed to upload mesh descriptor heap!");
    return false;
  }

  data.descriptor_heap_buffer = *descriptor_heap_buffer;
  return true;
}

auto upload_mesh_buffers(Init &init, RenderData &data) -> bool
{
  destroy_mesh_buffers(init, data);

  if (data.mesh.vertex_count() == 0) { return true; }

  if (data.mesh.positions.size() != data.mesh.colors.size()) {
    std::println("Mesh positions/colors size mismatch!");
    return false;
  }

  auto const vertex_count = data.mesh.vertex_count();
  if (data.mesh_buffer_vertex_capacity < vertex_count) {
    data.mesh_buffer_vertex_capacity =
      std::max({ vertex_count, data.mesh_buffer_vertex_capacity * 2, k_mesh_buffer_min_vertex_capacity });
  }

  auto const position_buffer_size = static_cast<VkDeviceSize>(
    data.mesh_buffer_vertex_capacity * sizeof(data.mesh.positions.front()));
  auto const color_buffer_size =
    static_cast<VkDeviceSize>(data.mesh_buffer_vertex_capacity * sizeof(data.mesh.colors.front()));

  auto position_buffer = init.gpu_allocator.create_storage_buffer(position_buffer_size);
  auto color_buffer = init.gpu_allocator.create_storage_buffer(color_buffer_size);
  if (!position_buffer || !color_buffer) {
    std::println("Failed to create mesh vertex buffers!");
    return false;
  }

  if (!init.gpu_allocator.write_buffer(
        *position_buffer, std::span<const std::array<f32, 2>>{ data.mesh.positions })) {
    std::println("Failed to upload position buffer!");
    return false;
  }

  if (!init.gpu_allocator.write_buffer(*color_buffer, std::span<const std::array<f32, 3>>{ data.mesh.colors })) {
    std::println("Failed to upload color buffer!");
    return false;
  }

  data.position_buffer = *position_buffer;
  data.color_buffer = *color_buffer;
  return true;
}

}// namespace vkgsplat
