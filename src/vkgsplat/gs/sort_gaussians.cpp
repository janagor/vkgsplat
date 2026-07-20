#include "gs/sort_gaussians.hpp"

#include "app_state.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/push_constants.hpp"
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

void compute_barrier(Init const &init, VkCommandBuffer command_buffer)
{
  VkMemoryBarrier const barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
  };
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0,
    1,
    &barrier,
    0,
    nullptr,
    0,
    nullptr);
}

void push_sort_constants(Init const &init,
  SortPushConstants const &push_constants,
  VkCommandBuffer command_buffer)
{
  VkPushDataInfoEXT const push_info = {
    .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
    .pNext = nullptr,
    .offset = 0,
    .data = { .address = &push_constants, .size = sizeof(SortPushConstants) },
  };
  init.cmd_push_data(command_buffer, &push_info);
}

void push_bitonic_constants(Init const &init,
  BitonicPushConstants const &push_constants,
  VkCommandBuffer command_buffer)
{
  VkPushDataInfoEXT const push_info = {
    .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
    .pNext = nullptr,
    .offset = 0,
    .data = { .address = &push_constants, .size = sizeof(BitonicPushConstants) },
  };
  init.cmd_push_data(command_buffer, &push_info);
}

// Flatten a large 1D thread count into a 2D workgroup grid within device limits.
// Shaders must recover: id = gx + gy * (NumWorkGroups.x * WorkGroupSize.x).
struct Dispatch2D
{
  u32 group_count_x{};
  u32 group_count_y{};
};

[[nodiscard]] auto dispatch_2d_for_threads(Init const &init, u32 thread_count, u32 local_size_x)
  -> Dispatch2D
{
  u32 const groups = (thread_count + local_size_x - 1U) / local_size_x;
  u32 const max_x = init.device.physical_device.properties.limits.maxComputeWorkGroupCount[0];
  u32 const max_y = init.device.physical_device.properties.limits.maxComputeWorkGroupCount[1];
  if (groups <= max_x) { return { .group_count_x = groups, .group_count_y = 1U }; }

  u32 const group_count_x = max_x;
  u32 const group_count_y = (groups + max_x - 1U) / max_x;
  if (group_count_y > max_y) {
    std::println(
      "Sort dispatch needs {}x{} groups but device max is {}x{}", group_count_x, group_count_y, max_x, max_y);
  }
  return { .group_count_x = group_count_x, .group_count_y = group_count_y };
}

void dispatch_compute_2d(Init const &init, VkCommandBuffer command_buffer, Dispatch2D const grid)
{
  init.disp.cmdDispatch(command_buffer, grid.group_count_x, grid.group_count_y, 1U);
}

}// namespace

auto init_sort_gaussians(Init &init, RenderData &data) -> bool
{
  if (!create_sort_buffers(init, data)) { return false; }

  std::array<uint32_t, 1> const sort_size_spec{ data.gaussian_sort_size };
  std::string const prepare_path = std::string(SHADER_DIRECTORY) + "/prepare_gaussian_sort.comp.spv";
  std::string const sort_path = std::string(SHADER_DIRECTORY) + "/sort_gaussians.comp.spv";
  std::string const identify_path = std::string(SHADER_DIRECTORY) + "/identify_tile_ranges.comp.spv";

  if (!data.prepare_sort_algorithm.init(init, prepare_path, std::span{ sort_size_spec })
      || !data.gaussian_sort_algorithm.init(init, sort_path, std::span{ sort_size_spec })
      || !data.identify_ranges_algorithm.init(init, identify_path)) {
    destroy_sort_buffers(init, data);
    return false;
  }

  return refresh_descriptor_heap(init, data);
}

void dispatch_sort_gaussians(Init const &init,
  RenderData const &data,
  SortPushConstants const &push_constants,
  VkCommandBuffer command_buffer)
{
  constexpr u32 k_local_size_x = 64U;
  Dispatch2D const sort_grid =
    dispatch_2d_for_threads(init, data.gaussian_sort_size, k_local_size_x);

  init.disp.cmdBindPipeline(
    command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.prepare_sort_algorithm.pipeline());
  push_sort_constants(init, push_constants, command_buffer);
  dispatch_compute_2d(init, command_buffer, sort_grid);
  compute_barrier(init, command_buffer);

  init.disp.cmdBindPipeline(
    command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.gaussian_sort_algorithm.pipeline());
  for (u32 k = 2U; k <= data.gaussian_sort_size; k <<= 1U) {
    for (u32 j = k >> 1U; j > 0U; j >>= 1U) {
      BitonicPushConstants const bitonic_push{
        .sort_size = data.gaussian_sort_size,
        .k = k,
        .j = j,
        .pad = 0U,
      };
      push_bitonic_constants(init, bitonic_push, command_buffer);
      dispatch_compute_2d(init, command_buffer, sort_grid);
      compute_barrier(init, command_buffer);
    }
  }

  init.disp.cmdFillBuffer(command_buffer,
    data.tile_ranges_buffer.handle,
    0,
    static_cast<VkDeviceSize>(data.tile_count * sizeof(TileRange)),
    0);
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

  init.disp.cmdBindPipeline(
    command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.identify_ranges_algorithm.pipeline());
  push_sort_constants(init, push_constants, command_buffer);
  dispatch_compute_2d(init, command_buffer, sort_grid);

  VkMemoryBarrier const done_barrier = {
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
    &done_barrier,
    0,
    nullptr,
    0,
    nullptr);
}

void destroy_sort_gaussians(Init &init, RenderData &data)
{
  data.prepare_sort_algorithm.destroy(init);
  data.gaussian_sort_algorithm.destroy(init);
  data.identify_ranges_algorithm.destroy(init);
  destroy_sort_buffers(init, data);
}

}// namespace vkgsplat::gs
