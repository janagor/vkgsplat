#include "gs/binning.hpp"

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/push_constants.hpp"
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

void dispatch_binning(Init const &init,
  RenderData const &data,
  BinPushConstants const &push_constants,
  VkCommandBuffer command_buffer)
{
  // Reset instance counter for this frame.
  init.disp.cmdFillBuffer(command_buffer, data.instance_count_buffer.handle, 0, sizeof(u32), 0);

  VkMemoryBarrier const clear_barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
  };
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0,
    1,
    &clear_barrier,
    0,
    nullptr,
    0,
    nullptr);

  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.bin_algorithm.pipeline());

  VkPushDataInfoEXT const push_info = {
    .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
    .pNext = nullptr,
    .offset = 0,
    .data = { .address = &push_constants, .size = sizeof(BinPushConstants) },
  };
  init.cmd_push_data(command_buffer, &push_info);

  uint32_t const workgroup_count = (data.splat_count + 63U) / 64U;
  init.disp.cmdDispatch(command_buffer, workgroup_count, 1U, 1U);

  VkMemoryBarrier const barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
  };
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0,
    1,
    &barrier,
    0,
    nullptr,
    0,
    nullptr);
}

void destroy_binning(Init &init, RenderData &data)
{
  data.bin_algorithm.destroy(init);
  destroy_bin_buffers(init, data);
}

}// namespace vkgsplat::gs
