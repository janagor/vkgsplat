#include "triangle_sort.hpp"


#include "compute/algorithm.hpp"
#include "compute/op_tensor_sync_device.hpp"
#include "compute/op_algo_dispatch.hpp"
#include "compute/param.hpp"
#include "compute/tensor.hpp"
#include "compute/sort_entry.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <print>
#include <string>
#include <utility>

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
  auto sort_entries = compute::tensor<compute::SortEntry>(init, k_sort_size);
  auto sorted_indices = compute::tensor<u32>(init, k_triangle_count, 0U);

  if (!sort_entries || !sorted_indices) {
    std::println("Failed to create triangle sort tensors!");
    return false;
  }

  data.sort_entries = std::move(*sort_entries);
  data.sorted_indices = std::move(*sorted_indices);

  if (!query_descriptor_heap_layout(init, data)) { return false; }

  compute::ParamList sort_params;
  sort_params.add(data.color_buffer, HeapSlot::Color)
    .add(data.sorted_indices, HeapSlot::SortedIndices)
    .add(data.sort_entries, HeapSlot::SortEntries);

  std::string const shader_path = std::string(SHADER_DIRECTORY) + "/sort_triangles.comp.spv";
  if (!data.sort_algorithm.init(init, data, shader_path, sort_params)) { return false; }

  auto sync_device = std::make_shared<compute::OpTensorSyncDevice>();
  sync_device->add(data.sort_entries);
  sync_device->add(data.sorted_indices);
  data.compute_sequence.record(sync_device);

  auto dispatch_op = std::make_shared<compute::OpAlgoDispatch>(data.sort_algorithm, std::array<uint32_t, 3>{ 1, 1, 1 });
  data.compute_sequence.record(dispatch_op);

  return refresh_descriptor_heap(init, data);
}

void dispatch_triangle_sort(Init &init, RenderData const &data, VkCommandBuffer command_buffer)
{ data.compute_sequence.eval(init, data, command_buffer); }

void destroy_triangle_sort(Init &init, RenderData &data)
{
  data.compute_sequence.clear();
  data.sort_algorithm.destroy(init);

  data.sort_entries.destroy(init);
  data.sorted_indices.destroy(init);
}

}// namespace vkgsplat
