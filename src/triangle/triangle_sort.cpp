#include "triangle_sort.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "descriptor_heap.hpp"
#include "initializers.hpp"
#include "mesh_gpu.hpp"
#include "shader.hpp"
#include "types.hpp"

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat {

namespace {

[[nodiscard]] auto create_sort_descriptor_heap(Init &init, RenderData &data) -> bool
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
  data.descriptor_stride =
    static_cast<size_t>(align_up(heap_props.bufferDescriptorSize, heap_props.bufferDescriptorAlignment));
  auto const descriptor_stride = data.descriptor_stride;
  VkDeviceSize const descriptor_region_size = descriptor_stride * 3;
  data.sort_reserved_range_offset = align_up(descriptor_region_size, heap_props.resourceHeapAlignment);
  data.sort_reserved_range_size = heap_props.minResourceHeapReservedRange;
  data.sort_descriptor_heap_size = data.sort_reserved_range_offset + data.sort_reserved_range_size;

  auto sort_descriptor_heap_buffer = init.gpu_allocator.create_heap_buffer(data.sort_descriptor_heap_size);
  if (!sort_descriptor_heap_buffer) {
    std::println("Failed to create sort descriptor heap buffer!");
    return false;
  }

  auto const color_buffer_size =
    static_cast<VkDeviceSize>(data.mesh_buffer_vertex_capacity * sizeof(data.mesh.colors.front()));
  auto const sort_entries_buffer_size = static_cast<VkDeviceSize>(k_sort_size * k_sort_entry_size);
  auto const sorted_indices_buffer_size = static_cast<VkDeviceSize>(k_triangle_count * sizeof(u32));

  std::vector<std::byte> descriptor_data(descriptor_stride * 3);
  std::array<VkDeviceAddressRangeEXT, 3> address_ranges = {
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.color_buffer),
      .size = color_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(data.sort_entries_buffer),
      .size = sort_entries_buffer_size,
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
          std::span{ descriptor_data }.subspan(i * descriptor_stride, descriptor_size))) {
      std::println("Failed to write sort buffer descriptor {}!", i);
      return false;
    }
  }

  if (!init.gpu_allocator.write_buffer<std::byte>(*sort_descriptor_heap_buffer, std::span{ descriptor_data })) {
    std::println("Failed to upload sort descriptor heap!");
    return false;
  }

  data.sort_descriptor_heap_buffer = *sort_descriptor_heap_buffer;
  return true;
}

void bind_sort_descriptor_heap(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  VkDeviceAddress const heap_address = init.gpu_allocator.get_buffer_device_address(data.sort_descriptor_heap_buffer);
  VkBindHeapInfoEXT const bind_heap_info = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = { .address = heap_address, .size = data.sort_descriptor_heap_size },
    .reservedRangeOffset = data.sort_reserved_range_offset,
    .reservedRangeSize = data.sort_reserved_range_size,
  };
  init.cmd_bind_resource_heap(command_buffer, &bind_heap_info);
}

}// namespace

auto init_triangle_sort(Init &init, RenderData &data) -> bool
{
  auto const sort_entries_buffer_size = static_cast<VkDeviceSize>(k_sort_size * k_sort_entry_size);
  auto const sorted_indices_buffer_size = static_cast<VkDeviceSize>(k_triangle_count * sizeof(u32));

  auto sort_entries_buffer = init.gpu_allocator.create_storage_buffer(sort_entries_buffer_size);
  auto sorted_indices_buffer = init.gpu_allocator.create_storage_buffer(sorted_indices_buffer_size);
  if (!sort_entries_buffer || !sorted_indices_buffer) {
    std::println("Failed to create triangle sort buffers!");
    return false;
  }

  data.sort_entries_buffer = *sort_entries_buffer;
  data.sorted_indices_buffer = *sorted_indices_buffer;

  if (!create_sort_descriptor_heap(init, data)) { return false; }

  auto const comp_code = read_file(std::string(EXAMPLE_SOURCE_DIRECTORY) + "/shaders/sort_triangles.comp.spv");
  VkShaderModule comp_module = create_shader_module(init, comp_code);
  if (comp_module == VK_NULL_HANDLE) {
    std::println("Failed to create sort compute shader module!");
    return false;
  }

  std::array<VkDescriptorSetAndBindingMappingEXT, 1> mappings = { VkDescriptorSetAndBindingMappingEXT{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
    .pNext = nullptr,
    .descriptorSet = 0,
    .firstBinding = 0,
    .bindingCount = 3,
    .resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT
                    | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
    .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
    .sourceData = { .constantOffset = { .heapOffset = 0,
                      .heapArrayStride = static_cast<uint32_t>(data.descriptor_stride),
                      .pEmbeddedSampler = nullptr,
                      .samplerHeapOffset = 0,
                      .samplerHeapArrayStride = 0 } },
  } };

  VkShaderDescriptorSetAndBindingMappingInfoEXT mapping_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
    .pNext = nullptr,
    .mappingCount = static_cast<uint32_t>(mappings.size()),
    .pMappings = mappings.data(),
  };

  VkPipelineShaderStageCreateInfo const comp_stage =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, comp_module, "main");
  VkPipelineShaderStageCreateInfo stage = comp_stage;
  stage.pNext = &mapping_info;

  VkPipelineCreateFlags2CreateInfo pipeline_flags = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
  };

  VkComputePipelineCreateInfo const pipeline_info = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = &pipeline_flags,
    .flags = 0,
    .stage = stage,
    .layout = VK_NULL_HANDLE,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = -1,
  };

  if (init.disp.createComputePipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &data.sort_compute_pipeline)
      != VK_SUCCESS) {
    std::println("Failed to create triangle sort compute pipeline!");
    init.disp.destroyShaderModule(comp_module, nullptr);
    return false;
  }

  init.disp.destroyShaderModule(comp_module, nullptr);

  return refresh_mesh_descriptor_heap(init, data);
}

void dispatch_triangle_sort(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  bind_sort_descriptor_heap(init, data, command_buffer);
  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.sort_compute_pipeline);
  init.disp.cmdDispatch(command_buffer, 1, 1, 1);

  VkMemoryBarrier const barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
  };

  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
    0,
    1,
    &barrier,
    0,
    nullptr,
    0,
    nullptr);
}

void destroy_triangle_sort(Init &init, RenderData &data)
{
  if (data.sort_compute_pipeline != VK_NULL_HANDLE) {
    init.disp.destroyPipeline(data.sort_compute_pipeline, nullptr);
    data.sort_compute_pipeline = VK_NULL_HANDLE;
  }

  init.gpu_allocator.destroy_buffer(data.sort_entries_buffer);
  init.gpu_allocator.destroy_buffer(data.sorted_indices_buffer);
  init.gpu_allocator.destroy_buffer(data.sort_descriptor_heap_buffer);
  data.sort_entries_buffer = {};
  data.sorted_indices_buffer = {};
  data.sort_descriptor_heap_buffer = {};
}

}// namespace vkgsplat
