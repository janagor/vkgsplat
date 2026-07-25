#include "gs/sorting.hpp"

#include "app_state.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

namespace {

  constexpr u32 k_radix_workgroup_size = 256U;
  constexpr u32 k_radix_bins = 256U;
  constexpr u32 k_radix_blocks_per_workgroup = 32U;

  [[nodiscard]] auto radix_workgroup_count(u32 num_elements) -> u32
  {
    if (num_elements == 0U) { return 1U; }
    u32 const threads = (num_elements + k_radix_blocks_per_workgroup - 1U) / k_radix_blocks_per_workgroup;
    u32 const wgs = (threads + k_radix_workgroup_size - 1U) / k_radix_workgroup_size;
    return wgs == 0U ? 1U : wgs;
  }

  void destroy_sort_buffers(Init &init, RenderData &data)
  {
    init.gpu_allocator.destroy_buffer(data.sorted_keys_buffer);
    init.gpu_allocator.destroy_buffer(data.sorted_values_buffer);
    init.gpu_allocator.destroy_buffer(data.sort_histogram_buffer);
    init.gpu_allocator.destroy_buffer(data.radix_dispatch_buffer);
    init.gpu_allocator.destroy_buffer(data.tile_ranges_buffer);
    data.sorted_keys_buffer = {};
    data.sorted_values_buffer = {};
    data.sort_histogram_buffer = {};
    data.radix_dispatch_buffer = {};
    data.tile_ranges_buffer = {};
    data.gaussian_sort_size = 0;
    data.radix_num_workgroups = 0;
    data.tile_count = 0;
  }

  [[nodiscard]] auto create_sort_buffers(Init &init, RenderData &data) -> bool
  {
    destroy_sort_buffers(init, data);

    if (data.max_bin_instances == 0) {
      std::println("Sort gaussians requires binning buffers first!");
      return false;
    }

    data.gaussian_sort_size = data.max_bin_instances;
    data.tile_count = k_max_tiles;
    data.radix_blocks_per_workgroup = k_radix_blocks_per_workgroup;
    // Histogram sized for worst-case capacity; live frames dispatch fewer groups.
    data.radix_num_workgroups = radix_workgroup_count(data.gaussian_sort_size);

    auto const keys_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(BinningKey));
    auto const values_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(u32));
    auto const histogram_entries =
      static_cast<size_t>(data.radix_num_workgroups) * static_cast<size_t>(k_radix_bins);
    auto const histogram_size = static_cast<VkDeviceSize>(histogram_entries * sizeof(u32));
    auto const ranges_size = static_cast<VkDeviceSize>(data.tile_count * sizeof(TileRange));
    auto const dispatch_size = static_cast<VkDeviceSize>(sizeof(VkDispatchIndirectCommand));

    auto keys = init.gpu_allocator.create_storage_buffer(keys_size);
    auto values = init.gpu_allocator.create_storage_buffer(values_size);
    auto histogram = init.gpu_allocator.create_storage_buffer(histogram_size);
    auto dispatch = init.gpu_allocator.create_storage_buffer(dispatch_size);
    auto ranges = init.gpu_allocator.create_storage_buffer(ranges_size);
    if (!keys || !values || !histogram || !dispatch || !ranges) {
      std::println("Failed to create sort buffers!");
      destroy_sort_buffers(init, data);
      return false;
    }

    data.sorted_keys_buffer = *keys;
    data.sorted_values_buffer = *values;
    data.sort_histogram_buffer = *histogram;
    data.radix_dispatch_buffer = *dispatch;
    data.tile_ranges_buffer = *ranges;

    std::vector<BinningKey> const zero_keys(data.gaussian_sort_size);
    std::vector<u32> const zero_values(data.gaussian_sort_size, 0U);
    std::vector<u32> const zero_histogram(histogram_entries, 0U);
    std::vector<TileRange> const zero_ranges(data.tile_count);
    VkDispatchIndirectCommand const zero_dispatch{ .x = 1U, .y = 1U, .z = 1U };
    if (!init.gpu_allocator.write_buffer(*keys, std::span{ zero_keys })
        || !init.gpu_allocator.write_buffer(*values, std::span{ zero_values })
        || !init.gpu_allocator.write_buffer(*histogram, std::span{ zero_histogram })
        || !init.gpu_allocator.write_buffer(*dispatch, std::span{ &zero_dispatch, 1 })
        || !init.gpu_allocator.write_buffer(*ranges, std::span{ zero_ranges })) {
      std::println("Failed to zero-initialize sort buffers!");
      destroy_sort_buffers(init, data);
      return false;
    }

    return true;
  }

}// namespace

auto init_sorting(Init &init, RenderData &data) -> bool
{
  if (!create_sort_buffers(init, data)) { return false; }

  std::array<uint32_t, 1> const sort_size_spec{ data.gaussian_sort_size };
  std::string const prepare_path = std::string(SHADER_DIRECTORY) + "/prepare_sorting.comp.spv";
  std::string const hist_path = std::string(SHADER_DIRECTORY) + "/multi_radixsort_histograms.comp.spv";
  std::string const scatter_path = std::string(SHADER_DIRECTORY) + "/multi_radixsort.comp.spv";
  std::string const identify_path = std::string(SHADER_DIRECTORY) + "/identify_ranges.comp.spv";

  if (!data.prepare_sort_algorithm.init(init, prepare_path, std::span{ sort_size_spec })
      || !data.radix_histogram_algorithm.init(init, hist_path)
      || !data.radix_scatter_algorithm.init(init, scatter_path)
      || !data.identify_ranges_algorithm.init(init, identify_path)) {
    destroy_sort_buffers(init, data);
    return false;
  }

  return refresh_descriptor_heap(init, data);
}

void destroy_sorting(Init &init, RenderData &data)
{
  data.prepare_sort_algorithm.destroy(init);
  data.radix_histogram_algorithm.destroy(init);
  data.radix_scatter_algorithm.destroy(init);
  data.identify_ranges_algorithm.destroy(init);
  destroy_sort_buffers(init, data);
}

}// namespace vkgsplat::gs
