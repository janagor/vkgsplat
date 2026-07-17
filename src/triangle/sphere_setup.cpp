#include "sphere_setup.hpp"

#include "app_state.hpp"
#include "compute/algorithm.hpp"
#include "compute/op_algo_dispatch.hpp"
#include "compute/op_tensor_sync_device.hpp"
#include "compute/param.hpp"
#include "compute/sort_entry.hpp"
#include "compute/tensor.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "types.hpp"
#include "vulkan_context.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <print>
#include <string>
#include <utility>


#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat {

auto init_sphere_setup(Init &init, RenderData &data) -> bool
{
  if (data.splat_count == 0 || data.sort_size == 0) {
    std::println("Sphere setup requires non-zero splat_count and sort_size!");
    return false;
  }

  auto sort_entries = compute::tensor<compute::SortEntry>(init, data.sort_size);
  auto sorted_indices = compute::tensor<u32>(init, data.splat_count, 0U);

  if (!sort_entries || !sorted_indices) {
    std::println("Failed to create sphere sort tensors!");
    return false;
  }

  data.sort_entries = std::move(*sort_entries);
  data.sorted_indices = std::move(*sorted_indices);

  if (!query_descriptor_heap_layout(init, data)) { return false; }

  auto const position_buffer_size =
    static_cast<VkDeviceSize>(data.splat_count * sizeof(std::array<f32, 3>));
  auto const color_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(f32));

  compute::ParamList setup_params;
  setup_params.add(data.position_buffer, position_buffer_size, HeapSlot::Position)
    .add(data.color_buffer, color_buffer_size, HeapSlot::Color)
    .add(data.sorted_indices, HeapSlot::SortedIndices)
    .add(data.sort_entries, HeapSlot::SortEntries);

  std::array<uint32_t, 3> const specialization_constants{
    data.splat_count,
    data.sort_size,
    data.procedural ? 1U : 0U,
  };

  std::string const shader_path = std::string(SHADER_DIRECTORY) + "/init_spheres.comp.spv";
  if (!data.sphere_setup_algorithm.init(
        init, data, shader_path, setup_params, std::span{ specialization_constants })) {
    return false;
  }

  auto sync_device = std::make_shared<compute::OpTensorSyncDevice>();
  sync_device->add(data.sort_entries);
  sync_device->add(data.sorted_indices);
  data.compute_sequence.record(sync_device);

  uint32_t const workgroup_count = (data.sort_size + 63U) / 64U;
  auto dispatch_op = std::make_shared<compute::OpAlgoDispatch>(
    data.sphere_setup_algorithm, std::array<uint32_t, 3>{ workgroup_count, 1U, 1U });
  data.compute_sequence.record(dispatch_op);

  return refresh_descriptor_heap(init, data);
}

void dispatch_sphere_setup(Init &init, RenderData const &data, VkCommandBuffer command_buffer)
{ data.compute_sequence.eval(init, data, command_buffer); }

void destroy_sphere_setup(Init &init, RenderData &data)
{
  data.compute_sequence.clear();
  data.sphere_setup_algorithm.destroy(init);

  data.sort_entries.destroy(init);
  data.sorted_indices.destroy(init);
}

}// namespace vkgsplat
