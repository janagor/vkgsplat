#include "gs/sorting.hpp"

#include "app_state.hpp"
#include "vulkan/descriptor/descriptor_heap.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/load_heap_pipeline.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <string>
#include <utility>

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

  void DestroySortBuffers(vulkan::Context &context, RenderData &data)
  {
    (void)context;
    data.sorted_keys_buffer.reset();
    data.sorted_values_buffer.reset();
    data.sort_histogram_buffer.reset();
    data.radix_dispatch_buffer.reset();
    data.draw_indirect_buffer.reset();
    data.tile_ranges_buffer.reset();
    data.gaussian_sort_size = 0;
    data.radix_num_workgroups = 0;
    data.tile_count = 0;
  }

  [[nodiscard]] auto CreateSortBuffers(vulkan::Context &context, RenderData &data) -> bool
  {
    DestroySortBuffers(context, data);

    if (context.vkexec_context == nullptr) {
      std::println("vkexec context missing for sort buffers!");
      return false;
    }
    auto &vkexec = *context.vkexec_context;

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

    auto keys = vulkan::CreateDeviceStorageBuffer(vkexec, keys_size);
    auto values = vulkan::CreateDeviceStorageBuffer(vkexec, values_size);
    auto histogram = vulkan::CreateDeviceStorageBuffer(vkexec, histogram_size);
    auto dispatch = vulkan::CreateDeviceStorageBuffer(vkexec, dispatch_size);
    auto draw_indirect = vulkan::CreateDeviceStorageBuffer(vkexec, draw_indirect_size);
    auto ranges = vulkan::CreateDeviceStorageBuffer(vkexec, ranges_size);
    if (!keys || !values || !histogram || !dispatch || !draw_indirect || !ranges) {
      std::println("Failed to create sort buffers!");
      DestroySortBuffers(context, data);
      return false;
    }

    data.sorted_keys_buffer = std::move(*keys);
    data.sorted_values_buffer = std::move(*values);
    data.sort_histogram_buffer = std::move(*histogram);
    data.radix_dispatch_buffer = std::move(*dispatch);
    data.draw_indirect_buffer = std::move(*draw_indirect);
    data.tile_ranges_buffer = std::move(*ranges);

    // GPU-only working set: shaders / cmdFillBuffer overwrite these each frame.
    return true;
  }

}// namespace

auto InitSorting(vulkan::Context &context, RenderData &data) -> bool
{
  if (!CreateSortBuffers(context, data)) { return false; }

  std::array<uint32_t, 1> const sort_size_spec{ data.gaussian_sort_size };
  std::string const prepare_path = std::string(kShaderDirectory) + "/prepare_sorting.comp.spv";
  std::string const hist_path = std::string(kShaderDirectory) + "/multi_radixsort_histograms.comp.spv";
  std::string const scatter_path = std::string(kShaderDirectory) + "/multi_radixsort.comp.spv";

  auto prepare = LoadHeapAlgorithm(context, prepare_path, std::span{ sort_size_spec });
  auto hist = LoadHeapAlgorithm(context, hist_path);
  auto scatter = LoadHeapAlgorithm(context, scatter_path);
  if (!prepare || !hist || !scatter) {
    DestroySortBuffers(context, data);
    return false;
  }
  data.prepare_sort_algorithm = std::move(*prepare);
  data.radix_histogram_algorithm = std::move(*hist);
  data.radix_scatter_algorithm = std::move(*scatter);

  return RefreshDescriptorHeap(context, data);
}

void DestroySorting(vulkan::Context &context, RenderData &data)
{
  data.prepare_sort_algorithm.reset();
  data.radix_histogram_algorithm.reset();
  data.radix_scatter_algorithm.reset();
  DestroySortBuffers(context, data);
}

}// namespace vkgsplat::gs
