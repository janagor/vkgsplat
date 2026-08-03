#include "gs/operations.hpp"

#include "app_state.hpp"
#include "backend/vulkan/gpu_pass_timer.hpp"
#include "backend/vulkan/sync_objects/barrier.hpp"
#include "gs/push_constants.hpp"
#include "gs/rasterization.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <cstddef>
#include <print>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

namespace {

  constexpr u32 kComputeLocalSizeX = 64U;

  struct Dispatch2D
  {
    u32 group_count_x{};
    u32 group_count_y{};
  };

  class ScopedGpuPass
  {
  public:
    ScopedGpuPass(Init const &init, RenderData const &data, VkCommandBuffer command_buffer, GpuPass pass)
      : init_(init), data_(data), command_buffer_(command_buffer), pass_(pass), active_(data.gpu_pass_timer.enabled())
    {
      if (active_) { data_.gpu_pass_timer.write(init_, data_.current_frame, pass_, false, command_buffer_); }
    }

    ScopedGpuPass(ScopedGpuPass const &) = delete;
    auto operator=(ScopedGpuPass const &) -> ScopedGpuPass & = delete;
    ScopedGpuPass(ScopedGpuPass &&) = delete;
    auto operator=(ScopedGpuPass &&) -> ScopedGpuPass & = delete;

    ~ScopedGpuPass()
    {
      if (active_) { data_.gpu_pass_timer.write(init_, data_.current_frame, pass_, true, command_buffer_); }
    }

  private:
    Init const &init_;
    RenderData const &data_;
    VkCommandBuffer command_buffer_;
    GpuPass pass_;
    bool active_;
  };

  void PushConstants(Init const &init, void const *data, size_t size, VkCommandBuffer command_buffer)
  {
    VkPushDataInfoEXT const push_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
      .pNext = nullptr,
      .offset = 0,
      .data = { .address = data, .size = size },
    };
    init.cmd_push_data(command_buffer, &push_info);
  }

  [[nodiscard]] auto Dispatch2dForThreads(Init const &init, u32 thread_count, u32 local_size_x) -> Dispatch2D
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

  void DispatchCompute1d(Init const &init, VkCommandBuffer command_buffer, u32 workgroup_count)
  { init.disp.cmdDispatch(command_buffer, workgroup_count, 1U, 1U); }

  void DispatchCompute2d(Init const &init, VkCommandBuffer command_buffer, Dispatch2D const grid)
  { init.disp.cmdDispatch(command_buffer, grid.group_count_x, grid.group_count_y, 1U); }

  void DispatchComputePass(Init const &init,
    VkCommandBuffer command_buffer,
    VkPipeline pipeline,
    void const *push_data,
    size_t push_size,
    Dispatch2D const grid)
  {
    init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    PushConstants(init, push_data, push_size, command_buffer);
    DispatchCompute2d(init, command_buffer, grid);
  }

}// namespace

void OpProjection::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  ScopedGpuPass const timer{ init, data, command_buffer, GpuPass::kProjection };

  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.project_algorithm.pipeline());
  PushConstants(init, &data.project_push, sizeof(ProjectPushConstants), command_buffer);

  u32 const workgroup_count = (data.splat_count + kComputeLocalSizeX - 1U) / kComputeLocalSizeX;
  DispatchCompute1d(init, command_buffer, workgroup_count);
  Barrier::compute_read(init.disp, command_buffer);
}

void OpBinning::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  ScopedGpuPass const timer{ init, data, command_buffer, GpuPass::kBinning };

  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.bin_algorithm.pipeline());
  PushConstants(init, &data.bin_push, sizeof(BinPushConstants), command_buffer);

  u32 const workgroup_count = (data.splat_count + kComputeLocalSizeX - 1U) / kComputeLocalSizeX;
  DispatchCompute1d(init, command_buffer, workgroup_count);
  Barrier::compute_read(init.disp, command_buffer);
}

void OpPrepareSort::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  ScopedGpuPass const timer{ init, data, command_buffer, GpuPass::kPrepareSort };

  Dispatch2D const sort_grid = Dispatch2dForThreads(init, data.gaussian_sort_size, kComputeLocalSizeX);

  DispatchComputePass(init,
    command_buffer,
    data.prepare_sort_algorithm.pipeline(),
    &data.sort_push,
    sizeof(SortPushConstants),
    sort_grid);
  Barrier::compute_to_compute(init.disp, command_buffer);
}

void OpRadixSort::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  ScopedGpuPass const timer{ init, data, command_buffer, GpuPass::kRadixSort };

  constexpr u32 kRadixPasses = 4U;// packed uint32 key, 8 bits per pass
  u64 const instance_count_address = init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer);

  for (u32 pass = 0U; pass < kRadixPasses; ++pass) {
    RadixPushConstants const radix_push{
      .instance_count_address = instance_count_address,
      .capacity = data.gaussian_sort_size,
      .shift = pass * 8U,
      .num_blocks_per_workgroup = data.radix_blocks_per_workgroup,
      .ping = pass & 1U,
      .pad0 = 0U,
      .pad1 = 0U,
    };

    init.disp.cmdBindPipeline(
      command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.radix_histogram_algorithm.pipeline());
    PushConstants(init, &radix_push, sizeof(RadixPushConstants), command_buffer);
    init.disp.cmdDispatchIndirect(command_buffer, data.radix_dispatch_buffer.handle, 0);
    Barrier::compute_to_compute(init.disp, command_buffer);

    init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.radix_scatter_algorithm.pipeline());
    PushConstants(init, &radix_push, sizeof(RadixPushConstants), command_buffer);
    init.disp.cmdDispatchIndirect(command_buffer, data.radix_dispatch_buffer.handle, 0);
    Barrier::compute_to_compute(init.disp, command_buffer);
  }
}

void OpRasterization::record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer)
{
  ScopedGpuPass const timer{ init, data, command_buffer, GpuPass::kRasterize };
  DispatchRasterization(init, data, data.raster_push, command_buffer, data.present_image_index);
}

}// namespace vkgsplat::gs
