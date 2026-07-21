#include "gs/binning.hpp"

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/types.hpp>

#include <array>
#include <cstdint>
#include <print>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

namespace {

  void destroy_bin_buffers(Init &init, RenderData &data)
  {
    init.gpu_allocator.destroy_buffer(data.unsorted_keys_buffer);
    init.gpu_allocator.destroy_buffer(data.unsorted_values_buffer);
    init.gpu_allocator.destroy_buffer(data.instance_count_buffer);
    data.unsorted_keys_buffer = {};
    data.unsorted_values_buffer = {};
    data.instance_count_buffer = {};
    data.max_bin_instances = 0;
  }

  [[nodiscard]] auto create_bin_buffers(Init &init, RenderData &data) -> bool
  {
    destroy_bin_buffers(init, data);

    data.max_bin_instances = data.splat_count * k_max_tiles_per_splat;
    if (data.max_bin_instances == 0) {
      std::println("Bin gaussians requires non-zero instance capacity!");
      return false;
    }

    auto const keys_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(BinningKey));
    auto const values_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(u32));
    auto const count_size = static_cast<VkDeviceSize>(sizeof(u32));

    auto keys = init.gpu_allocator.create_storage_buffer(keys_size);
    auto values = init.gpu_allocator.create_storage_buffer(values_size);
    auto count = init.gpu_allocator.create_storage_buffer(count_size);
    if (!keys || !values || !count) {
      std::println("Failed to create binning buffers!");
      destroy_bin_buffers(init, data);
      return false;
    }

    data.unsorted_keys_buffer = *keys;
    data.unsorted_values_buffer = *values;
    data.instance_count_buffer = *count;

    std::vector<BinningKey> const zero_keys(data.max_bin_instances);
    std::vector<u32> const zero_values(data.max_bin_instances, 0U);
    std::vector<u32> const zero_count(1U, 0U);
    if (!init.gpu_allocator.write_buffer(*keys, std::span{ zero_keys })
        || !init.gpu_allocator.write_buffer(*values, std::span{ zero_values })
        || !init.gpu_allocator.write_buffer(*count, std::span{ zero_count })) {
      std::println("Failed to zero-initialize binning buffers!");
      destroy_bin_buffers(init, data);
      return false;
    }

    return true;
  }

}// namespace

auto init_binning(Init &init, RenderData &data) -> bool
{
  if (data.splat_count == 0) {
    std::println("Bin gaussians requires non-zero splat_count!");
    return false;
  }

  if (!create_bin_buffers(init, data)) { return false; }

  std::array<uint32_t, 1> const specialization_constants{ data.splat_count };
  std::string const shader_path = std::string(SHADER_DIRECTORY) + "/binning.comp.spv";
  if (!data.bin_algorithm.init(init, shader_path, std::span{ specialization_constants })) {
    destroy_bin_buffers(init, data);
    return false;
  }

  return true;
}

void destroy_binning(Init &init, RenderData &data)
{
  data.bin_algorithm.destroy(init);
  destroy_bin_buffers(init, data);
}

}// namespace vkgsplat::gs
