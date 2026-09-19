#include "sphere_setup.hpp"

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan/descriptor/descriptor_heap.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/types.hpp>

#include <vkexec/sync_wait.hpp>
#include <vkexec/tensor.hpp>

#include <print>
#include <utility>

namespace vkgsplat {

auto InitSphereSetup(vulkan::Context &context, RenderData &data) -> bool
{
  if (data.splat_count == 0 || data.sort_size == 0) {
    std::println("Sphere setup requires non-zero splat_count and sort_size!");
    return false;
  }
  if (context.vkexec_context == nullptr) {
    std::println("vkexec context missing for sphere sort tensors!");
    return false;
  }

  auto sort_entries =
    vkexec::try_sync_wait_value(vkexec::tensor<gs::SortEntry>::create(*context.vkexec_context, data.sort_size));
  auto sorted_indices =
    vkexec::try_sync_wait_value(vkexec::tensor<u32>::create(*context.vkexec_context, data.splat_count, 0U));

  if (!sort_entries || !sorted_indices) {
    std::println("Failed to create sphere sort tensors!");
    return false;
  }

  if (auto uploaded = sort_entries->upload(*context.vkexec_context); !uploaded) {
    std::println("Failed to upload sort_entries: {}", uploaded.error().message());
    return false;
  }
  if (auto uploaded = sorted_indices->upload(*context.vkexec_context); !uploaded) {
    std::println("Failed to upload sorted_indices: {}", uploaded.error().message());
    return false;
  }

  data.sort_entries = std::move(*sort_entries);
  data.sorted_indices = std::move(*sorted_indices);

  return QueryDescriptorHeapLayout(context, data);
}

void DestroySphereSetup(vulkan::Context &context, RenderData &data)
{
  (void)context;
  data.sort_entries.reset();
  data.sorted_indices.reset();
}

}// namespace vkgsplat
