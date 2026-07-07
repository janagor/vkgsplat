#include "triangle_sort.hpp"


#include "compute/algorithm.hpp"
#include "compute/op_algo_dispatch.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <print>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "descriptor/descriptor_heap.hpp"
// #include "initializers.hpp"
// #include "shader.hpp"
#include "types.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat {

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

  if (!query_descriptor_heap_layout(init, data)) { return false; }

  std::vector<compute::DescriptorMapping> mappings = {
    {
      .binding = 0,
      .heap_offset = heap_slot_byte_offset(data, HeapSlot::Color),
    },
    {
      .binding = 1,
      .heap_offset = heap_slot_byte_offset(data, HeapSlot::SortedIndices),
    },
    {
      .binding = 2,
      .heap_offset = heap_slot_byte_offset(data, HeapSlot::SortEntries),
    },
  };

  std::string const shader_path = std::string(SHADER_DIRECTORY) + "/sort_triangles.comp.spv";
  if (!data.sort_algorithm.init(init, data, shader_path, mappings)) { return false; }

  auto dispatch_op = std::make_shared<compute::OpAlgoDispatch>(data.sort_algorithm, std::array<uint32_t, 3>{ 1, 1, 1 });
  data.compute_sequence.record(dispatch_op);

  return refresh_descriptor_heap(init, data);
}

void dispatch_triangle_sort(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{ data.compute_sequence.eval(init, data, command_buffer); }

void destroy_triangle_sort(Init &init, RenderData &data)
{
  data.compute_sequence.clear();
  data.sort_algorithm.destroy(init);

  init.gpu_allocator.destroy_buffer(data.sort_entries_buffer);
  init.gpu_allocator.destroy_buffer(data.sorted_indices_buffer);

  data.sort_entries_buffer = {};
  data.sorted_indices_buffer = {};
}

}// namespace vkgsplat
