#include "sphere_setup.hpp"

#include "app_state.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "compute/algorithm.hpp"
#include "compute/op_algo_dispatch.hpp"
#include "compute/sort_entry.hpp"
#include "compute/tensor.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstdint>
#include <print>
#include <string>
#include <utility>

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat {

auto InitSphereSetup(Init &init, RenderData &data) -> bool
{
  if (data.splat_count == 0 || data.sort_size == 0) {
    std::println("Sphere setup requires non-zero splat_count and sort_size!");
    return false;
  }

  auto sort_entries = compute::MakeTensor<compute::SortEntry>(init, data.sort_size);
  auto sorted_indices = compute::MakeTensor<u32>(init, data.splat_count, 0U);

  if (!sort_entries || !sorted_indices) {
    std::println("Failed to create sphere sort tensors!");
    return false;
  }

  data.sort_entries = std::move(*sort_entries);
  data.sorted_indices = std::move(*sorted_indices);

  if (!QueryDescriptorHeapLayout(init, data)) { return false; }

  std::array<uint32_t, 2> const specialization_constants{
    data.splat_count,
    data.sort_size,
  };

  std::string const shader_path = std::string(SHADER_DIRECTORY) + "/init_spheres.comp.spv";
  if (!data.sphere_setup_algorithm.init(init, shader_path, std::span{ specialization_constants })) { return false; }

  // Host mirrors are uploaded once in Tensor::create; do not re-sync every dispatch.
  uint32_t const workgroup_count = (data.sort_size + 63U) / 64U;
  data.compute_sequence.emplace<compute::OpAlgoDispatch>(
    data.sphere_setup_algorithm, std::array<uint32_t, 3>{ workgroup_count, 1U, 1U });

  return true;
}

void DispatchSphereSetup(Init &init, RenderData const &data, VkCommandBuffer command_buffer)
{ data.compute_sequence.eval(init, data, command_buffer); }

void DestroySphereSetup(Init &init, RenderData &data)
{
  data.compute_sequence.clear();
  data.sphere_setup_algorithm.destroy(init);

  data.sort_entries.destroy(init);
  data.sorted_indices.destroy(init);
}

}// namespace vkgsplat
