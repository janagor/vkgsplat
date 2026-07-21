#include "gs/sorting.hpp"

#include "app_state.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/types.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <print>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

namespace {

  [[nodiscard]] auto next_power_of_2(u32 value) -> u32
  {
    if (value <= 1U) { return 1U; }
    return 1U << static_cast<unsigned>(std::bit_width(static_cast<unsigned>(value - 1U)));
  }

  void destroy_sort_buffers(Init &init, RenderData &data)
  {
    init.gpu_allocator.destroy_buffer(data.sorted_keys_buffer);
    init.gpu_allocator.destroy_buffer(data.sorted_values_buffer);
    init.gpu_allocator.destroy_buffer(data.tile_ranges_buffer);
    data.sorted_keys_buffer = {};
    data.sorted_values_buffer = {};
    data.tile_ranges_buffer = {};
    data.gaussian_sort_size = 0;
    data.tile_count = 0;
  }

  [[nodiscard]] auto create_sort_buffers(Init &init, RenderData &data) -> bool
  {
    destroy_sort_buffers(init, data);

    if (data.max_bin_instances == 0) {
      std::println("Sort gaussians requires binning buffers first!");
      return false;
    }

    data.gaussian_sort_size = next_power_of_2(data.max_bin_instances);
    data.tile_count = k_max_tiles;

    auto const keys_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(BinningKey));
    auto const values_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(u32));
    auto const ranges_size = static_cast<VkDeviceSize>(data.tile_count * sizeof(TileRange));

    auto keys = init.gpu_allocator.create_storage_buffer(keys_size);
    auto values = init.gpu_allocator.create_storage_buffer(values_size);
    auto ranges = init.gpu_allocator.create_storage_buffer(ranges_size);
    if (!keys || !values || !ranges) {
      std::println("Failed to create sort buffers!");
      destroy_sort_buffers(init, data);
      return false;
    }

    data.sorted_keys_buffer = *keys;
    data.sorted_values_buffer = *values;
    data.tile_ranges_buffer = *ranges;

    std::vector<BinningKey> const zero_keys(data.gaussian_sort_size);
    std::vector<u32> const zero_values(data.gaussian_sort_size, 0U);
    std::vector<TileRange> const zero_ranges(data.tile_count);
    if (!init.gpu_allocator.write_buffer(*keys, std::span{ zero_keys })
        || !init.gpu_allocator.write_buffer(*values, std::span{ zero_values })
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
  std::string const sort_path = std::string(SHADER_DIRECTORY) + "/sorting.comp.spv";
  std::string const identify_path = std::string(SHADER_DIRECTORY) + "/identify_ranges.comp.spv";

  if (!data.prepare_sort_algorithm.init(init, prepare_path, std::span{ sort_size_spec })
      || !data.gaussian_sort_algorithm.init(init, sort_path, std::span{ sort_size_spec })
      || !data.identify_ranges_algorithm.init(init, identify_path)) {
    destroy_sort_buffers(init, data);
    return false;
  }

  return refresh_descriptor_heap(init, data);
}

void destroy_sorting(Init &init, RenderData &data)
{
  data.prepare_sort_algorithm.destroy(init);
  data.gaussian_sort_algorithm.destroy(init);
  data.identify_ranges_algorithm.destroy(init);
  destroy_sort_buffers(init, data);
}

}// namespace vkgsplat::gs
