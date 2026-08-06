#include "gs/binning.hpp"

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstdint>
#include <print>
#include <span>
#include <string>

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

namespace {

  void DestroyBinBuffers(Init &init, RenderData &data)
  {
    init.gpu_allocator.destroy_buffer(data.unsorted_keys_buffer);
    init.gpu_allocator.destroy_buffer(data.unsorted_values_buffer);
    init.gpu_allocator.destroy_buffer(data.instance_count_buffer);
    data.unsorted_keys_buffer = {};
    data.unsorted_values_buffer = {};
    data.instance_count_buffer = {};
    data.max_bin_instances = 0;
  }

  [[nodiscard]] auto CreateBinBuffers(Init &init, RenderData &data) -> bool
  {
    DestroyBinBuffers(init, data);

    data.max_bin_instances = data.splat_count;
    if (data.max_bin_instances == 0) {
      std::println("Bin gaussians requires non-zero instance capacity!");
      return false;
    }

    auto const keys_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(BinningKey));
    auto const values_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(u32));
    auto const count_size = static_cast<VkDeviceSize>(sizeof(u32));

    auto keys = init.gpu_allocator.create_device_storage_buffer(keys_size);
    auto values = init.gpu_allocator.create_device_storage_buffer(values_size);
    auto count = init.gpu_allocator.create_device_storage_buffer(count_size);
    if (!keys || !values || !count) {
      std::println("Failed to create binning buffers!");
      DestroyBinBuffers(init, data);
      return false;
    }

    data.unsorted_keys_buffer = *keys;
    data.unsorted_values_buffer = *values;
    data.instance_count_buffer = *count;

    // GPU-only: instance count is cmdFillBuffer'd and keys/values rewritten each frame.
    return true;
  }

}// namespace

auto InitBinning(Init &init, RenderData &data) -> bool
{
  if (data.splat_count == 0) {
    std::println("Bin gaussians requires non-zero splat_count!");
    return false;
  }

  if (!CreateBinBuffers(init, data)) { return false; }

  std::array<uint32_t, 1> const specialization_constants{ data.splat_count };
  std::string const shader_path = std::string(kShaderDirectory) + "/binning.comp.spv";
  if (!data.bin_algorithm.init(init, shader_path, std::span{ specialization_constants })) {
    DestroyBinBuffers(init, data);
    return false;
  }

  return true;
}

void DestroyBinning(Init &init, RenderData &data)
{
  data.bin_algorithm.destroy(init);
  DestroyBinBuffers(init, data);
}

}// namespace vkgsplat::gs
