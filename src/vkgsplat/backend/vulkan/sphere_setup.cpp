#include "sphere_setup.hpp"

#include "app_state.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "compute/sort_entry.hpp"
#include "compute/tensor.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/types.hpp>

#include <print>
#include <utility>

namespace vkgsplat {

auto InitSphereSetup(vulkan::Context &context, RenderData &data) -> bool
{
  if (data.splat_count == 0 || data.sort_size == 0) {
    std::println("Sphere setup requires non-zero splat_count and sort_size!");
    return false;
  }

  auto sort_entries = compute::MakeTensor<compute::SortEntry>(context, data.sort_size);
  auto sorted_indices = compute::MakeTensor<u32>(context, data.splat_count, 0U);

  if (!sort_entries || !sorted_indices) {
    std::println("Failed to create sphere sort tensors!");
    return false;
  }

  data.sort_entries = std::move(*sort_entries);
  data.sorted_indices = std::move(*sorted_indices);

  return QueryDescriptorHeapLayout(context, data);
}

void DestroySphereSetup(vulkan::Context &context, RenderData &data)
{
  data.sort_entries.destroy(context);
  data.sorted_indices.destroy(context);
}

}// namespace vkgsplat
