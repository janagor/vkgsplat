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

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

namespace {

  constexpr u32 kRadixWorkgroupSize = 256U;
  constexpr u32 kRadixBins = 256U;

  [[nodiscard]] auto RadixWorkgroupCount(u32 num_elements) -> u32
  {
    if (num_elements == 0U) { return 1U; }
    u32 const threads = (num_elements + kRadixBlocksPerWorkgroup - 1U) / kRadixBlocksPerWorkgroup;
    u32 const wgs = (threads + kRadixWorkgroupSize - 1U) / kRadixWorkgroupSize;
    return wgs == 0U ? 1U : wgs;
  }

  void DestroySortBuffers(Init &init, RenderData &data)
  {
    init.gpu_allocator.destroy_buffer(data.sorted_keys_buffer);
    init.gpu_allocator.destroy_buffer(data.sorted_values_buffer);
    init.gpu_allocator.destroy_buffer(data.sort_histogram_buffer);
    init.gpu_allocator.destroy_buffer(data.radix_dispatch_buffer);
    init.gpu_allocator.destroy_buffer(data.draw_indirect_buffer);
    init.gpu_allocator.destroy_buffer(data.tile_ranges_buffer);
    data.sorted_keys_buffer = {};
    data.sorted_values_buffer = {};
    data.sort_histogram_buffer = {};
    data.radix_dispatch_buffer = {};
    data.draw_indirect_buffer = {};
    data.tile_ranges_buffer = {};
    data.gaussian_sort_size = 0;
    data.radix_num_workgroups = 0;
    data.tile_count = 0;
  }

  [[nodiscard]] auto CreateSortBuffers(Init &init, RenderData &data) -> bool
  {
    DestroySortBuffers(init, data);

    if (data.max_bin_instances == 0) {
      std::println("Sort gaussians requires binning buffers first!");
      return false;
    }

    data.gaussian_sort_size = data.max_bin_instances;
    data.tile_count = kMaxTiles;
    data.radix_blocks_per_workgroup = kRadixBlocksPerWorkgroup;
    // Histogram sized for worst-case capacity; live frames dispatch fewer groups.
    data.radix_num_workgroups = RadixWorkgroupCount(data.gaussian_sort_size);

    auto const keys_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(BinningKey));
    auto const values_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(u32));
    auto const histogram_entries = static_cast<size_t>(data.radix_num_workgroups) * static_cast<size_t>(kRadixBins);
    auto const histogram_size = static_cast<VkDeviceSize>(histogram_entries * sizeof(u32));
    auto const ranges_size = static_cast<VkDeviceSize>(data.tile_count * sizeof(TileRange));
    auto const dispatch_size = static_cast<VkDeviceSize>(sizeof(VkDispatchIndirectCommand));
    auto const draw_indirect_size = static_cast<VkDeviceSize>(sizeof(VkDrawIndirectCommand));

    auto keys = init.gpu_allocator.create_device_storage_buffer(keys_size);
    auto values = init.gpu_allocator.create_device_storage_buffer(values_size);
    auto histogram = init.gpu_allocator.create_device_storage_buffer(histogram_size);
    auto dispatch = init.gpu_allocator.create_device_storage_buffer(dispatch_size);
    auto draw_indirect = init.gpu_allocator.create_device_storage_buffer(draw_indirect_size);
    auto ranges = init.gpu_allocator.create_device_storage_buffer(ranges_size);
    if (!keys || !values || !histogram || !dispatch || !draw_indirect || !ranges) {
      std::println("Failed to create sort buffers!");
      DestroySortBuffers(init, data);
      return false;
    }

    data.sorted_keys_buffer = *keys;
    data.sorted_values_buffer = *values;
    data.sort_histogram_buffer = *histogram;
    data.radix_dispatch_buffer = *dispatch;
    data.draw_indirect_buffer = *draw_indirect;
    data.tile_ranges_buffer = *ranges;

    // GPU-only working set: shaders / cmdFillBuffer overwrite these each frame.
    return true;
  }

}// namespace

auto InitSorting(Init &init, RenderData &data) -> bool
{
  if (!CreateSortBuffers(init, data)) { return false; }

  std::array<uint32_t, 1> const sort_size_spec{ data.gaussian_sort_size };
  std::string const prepare_path = std::string(kShaderDirectory) + "/prepare_sorting.comp.spv";
  std::string const hist_path = std::string(kShaderDirectory) + "/multi_radixsort_histograms.comp.spv";
  std::string const scatter_path = std::string(kShaderDirectory) + "/multi_radixsort.comp.spv";

  if (!data.prepare_sort_algorithm.init(init, prepare_path, std::span{ sort_size_spec })
      || !data.radix_histogram_algorithm.init(init, hist_path)
      || !data.radix_scatter_algorithm.init(init, scatter_path)) {
    DestroySortBuffers(init, data);
    return false;
  }

  return RefreshDescriptorHeap(init, data);
}

void DestroySorting(Init &init, RenderData &data)
{
  data.prepare_sort_algorithm.destroy(init);
  data.radix_histogram_algorithm.destroy(init);
  data.radix_scatter_algorithm.destroy(init);
  DestroySortBuffers(init, data);
}

}// namespace vkgsplat::gs
