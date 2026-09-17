#include "gs/binning.hpp"

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/load_heap_pipeline.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstdint>
#include <print>
#include <span>
#include <string>
#include <utility>

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

namespace {

  void DestroyBinBuffers(vulkan::Context &context, RenderData &data)
  {
    (void)context;
    data.unsorted_keys_buffer.reset();
    data.unsorted_values_buffer.reset();
    data.instance_count_buffer.reset();
    data.max_bin_instances = 0;
  }

  [[nodiscard]] auto CreateBinBuffers(vulkan::Context &context, RenderData &data) -> bool
  {
    DestroyBinBuffers(context, data);

    if (context.vkexec_context == nullptr) {
      std::println("vkexec context missing for binning buffers!");
      return false;
    }
    auto &vkexec = *context.vkexec_context;

    data.max_bin_instances = data.splat_count;
    if (data.max_bin_instances == 0) {
      std::println("Bin gaussians requires non-zero instance capacity!");
      return false;
    }

    auto const keys_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(BinningKey));
    auto const values_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(u32));
    auto const count_size = static_cast<VkDeviceSize>(sizeof(u32));

    auto keys = vulkan::CreateDeviceStorageBuffer(vkexec, keys_size);
    auto values = vulkan::CreateDeviceStorageBuffer(vkexec, values_size);
    auto count = vulkan::CreateDeviceStorageBuffer(vkexec, count_size);
    if (!keys || !values || !count) {
      std::println("Failed to create binning buffers!");
      DestroyBinBuffers(context, data);
      return false;
    }

    data.unsorted_keys_buffer = std::move(*keys);
    data.unsorted_values_buffer = std::move(*values);
    data.instance_count_buffer = std::move(*count);

    // GPU-only: instance count is cmdFillBuffer'd and keys/values rewritten each frame.
    return true;
  }

}// namespace

auto InitBinning(vulkan::Context &context, RenderData &data) -> bool
{
  if (data.splat_count == 0) {
    std::println("Bin gaussians requires non-zero splat_count!");
    return false;
  }

  if (!CreateBinBuffers(context, data)) { return false; }

  std::array<uint32_t, 1> const specialization_constants{ data.splat_count };
  std::string const shader_path = std::string(kShaderDirectory) + "/binning.comp.spv";
  auto algorithm = LoadHeapAlgorithm(context, shader_path, std::span{ specialization_constants });
  if (!algorithm) {
    DestroyBinBuffers(context, data);
    return false;
  }
  data.bin_algorithm = std::move(*algorithm);
  return true;
}

void DestroyBinning(vulkan::Context &context, RenderData &data)
{
  data.bin_algorithm.reset();
  DestroyBinBuffers(context, data);
}

}// namespace vkgsplat::gs
