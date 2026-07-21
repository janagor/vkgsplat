#include "gs/operations.hpp"

#include "app_state.hpp"
#include "backend/vulkan/sync_objects/barrier.hpp"
#include "gs/push_constants.hpp"
#include "gs/rasterization.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/types.hpp>

#include <cstddef>
#include <print>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

namespace {

  constexpr u32 k_compute_local_size_x = 64U;

  struct Dispatch2D
  {
    u32 group_count_x{};
    u32 group_count_y{};
  };

  void push_constants(Init const &init, void const *data, size_t size, VkCommandBuffer command_buffer)
  {
    VkPushDataInfoEXT const push_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
      .pNext = nullptr,
      .offset = 0,
      .data = { .address = data, .size = size },
    };
    init.cmd_push_data(command_buffer, &push_info);
  }

  [[nodiscard]] auto dispatch_2d_for_threads(Init const &init, u32 thread_count, u32 local_size_x) -> Dispatch2D
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

  void dispatch_compute_1d(Init const &init, VkCommandBuffer command_buffer, u32 workgroup_count)
  { init.disp.cmdDispatch(command_buffer, workgroup_count, 1U, 1U); }

  void dispatch_compute_2d(Init const &init, VkCommandBuffer command_buffer, Dispatch2D const grid)
  { init.disp.cmdDispatch(command_buffer, grid.group_count_x, grid.group_count_y, 1U); }

  void dispatch_compute_pass(Init const &init,
    VkCommandBuffer command_buffer,
    VkPipeline pipeline,
    void const *push_data,
    size_t push_size,
    Dispatch2D const grid)
  {
    init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push_constants(init, push_data, push_size, command_buffer);
    dispatch_compute_2d(init, command_buffer, grid);
  }

}// namespace

void OpProjection::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.project_algorithm.pipeline());
  push_constants(init, &data.project_push, sizeof(ProjectPushConstants), command_buffer);

  u32 const workgroup_count = (data.splat_count + k_compute_local_size_x - 1U) / k_compute_local_size_x;
  dispatch_compute_1d(init, command_buffer, workgroup_count);
  Barrier::compute_read(init.disp, command_buffer);
}

void OpBinning::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.bin_algorithm.pipeline());
  push_constants(init, &data.bin_push, sizeof(BinPushConstants), command_buffer);

  u32 const workgroup_count = (data.splat_count + k_compute_local_size_x - 1U) / k_compute_local_size_x;
  dispatch_compute_1d(init, command_buffer, workgroup_count);
  Barrier::compute_read(init.disp, command_buffer);
}

void OpPrepareSort::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  Dispatch2D const sort_grid = dispatch_2d_for_threads(init, data.gaussian_sort_size, k_compute_local_size_x);

  dispatch_compute_pass(init,
    command_buffer,
    data.prepare_sort_algorithm.pipeline(),
    &data.sort_push,
    sizeof(SortPushConstants),
    sort_grid);
  Barrier::compute_to_compute(init.disp, command_buffer);
}

void OpBitonicSort::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  Dispatch2D const sort_grid = dispatch_2d_for_threads(init, data.gaussian_sort_size, k_compute_local_size_x);

  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.gaussian_sort_algorithm.pipeline());
  for (u32 k = 2U; k <= data.gaussian_sort_size; k <<= 1U) {
    for (u32 j = k >> 1U; j > 0U; j >>= 1U) {
      BitonicPushConstants const bitonic_push{
        .sort_size = data.gaussian_sort_size,
        .k = k,
        .j = j,
        .pad = 0U,
      };
      push_constants(init, &bitonic_push, sizeof(BitonicPushConstants), command_buffer);
      dispatch_compute_2d(init, command_buffer, sort_grid);
      Barrier::compute_to_compute(init.disp, command_buffer);
    }
  }
}

void OpIdentifyRanges::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  Dispatch2D const sort_grid = dispatch_2d_for_threads(init, data.gaussian_sort_size, k_compute_local_size_x);

  dispatch_compute_pass(init,
    command_buffer,
    data.identify_ranges_algorithm.pipeline(),
    &data.sort_push,
    sizeof(SortPushConstants),
    sort_grid);
  Barrier::compute_read(init.disp, command_buffer);
}

void OpRasterization::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  dispatch_rasterization(init, data, data.raster_push, command_buffer, data.present_image_index);
}

}// namespace vkgsplat::gs
