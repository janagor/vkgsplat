#include "gs/operations.hpp"

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "gs/rasterization.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <print>
#include <span>

#include <vkexec/barrier.hpp>
#include <vkexec/pass.hpp>
#include <vkexec_extensions/descriptor_heap/algorithm.hpp>
#include <vkexec_extensions/descriptor_heap/pass.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

namespace {

  constexpr u32 kComputeLocalSizeX = 64U;

  class ScopedGpuPass
  {
  public:
    ScopedGpuPass(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer, GpuPass pass)
      : context_(context), data_(data), command_buffer_(command_buffer), pass_(pass),
        active_(data.gpu_pass_timer.enabled())
    {
      if (active_) { data_.gpu_pass_timer.write(context_, data_.current_slot, pass_, false, command_buffer_); }
    }

    ScopedGpuPass(ScopedGpuPass const &) = delete;
    auto operator=(ScopedGpuPass const &) -> ScopedGpuPass & = delete;
    ScopedGpuPass(ScopedGpuPass &&) = delete;
    auto operator=(ScopedGpuPass &&) -> ScopedGpuPass & = delete;

    ~ScopedGpuPass()
    {
      if (active_) { data_.gpu_pass_timer.write(context_, data_.current_slot, pass_, true, command_buffer_); }
    }

  private:
    vulkan::Context const &context_;
    RenderData const &data_;
    VkCommandBuffer command_buffer_;
    GpuPass pass_;
    bool active_;
  };

  template<typename Params>
  void RecordHeapDispatch(vulkan::Context const &context,
    VkCommandBuffer command_buffer,
    vkexec::algorithm const &algorithm,
    Params const &params,
    u32 work_count)
  {
    (void)vkexec::record_pass(*context.vkexec_context,
      command_buffer,
      algorithm.bind(),
      std::as_bytes(std::span{ &params, 1 }),
      algorithm.groups_for(work_count));
  }

  template<typename Params>
  void RecordHeapIndirect(vulkan::Context const &context,
    VkCommandBuffer command_buffer,
    vkexec::algorithm const &algorithm,
    Params const &params,
    VkBuffer indirect_buffer)
  {
    (void)vkexec::record_pass(*context.vkexec_context,
      command_buffer,
      algorithm.bind(),
      std::as_bytes(std::span{ &params, 1 }),
      vkexec::indirect_dispatch{ .buffer = indirect_buffer, .offset = 0 });
  }

  [[nodiscard]] auto Dispatch2dForThreads(vulkan::Context const &context, u32 thread_count, u32 local_size_x)
    -> vkexec::dispatch
  {
    u32 const groups = (thread_count + local_size_x - 1U) / local_size_x;
    u32 const max_x = context.device.physical_device.properties.limits.maxComputeWorkGroupCount[0];
    u32 const max_y = context.device.physical_device.properties.limits.maxComputeWorkGroupCount[1];
    if (groups <= max_x) { return vkexec::dispatch_groups_for(thread_count, local_size_x); }

    u32 const group_count_x = max_x;
    u32 const group_count_y = (groups + max_x - 1U) / max_x;
    if (group_count_y > max_y) {
      std::println(
        "Sort dispatch needs {}x{} groups but device max is {}x{}", group_count_x, group_count_y, max_x, max_y);
    }
    return vkexec::dispatch{ .x = group_count_x, .y = group_count_y, .z = 1U };
  }

}// namespace

void RecordProjection(vulkan::Context const &context,
  RenderData const &data,
  VkCommandBuffer command_buffer,
  bool time_pass)
{
  if (!data.project_algorithm || context.vkexec_context == nullptr) { return; }

  if (time_pass) { data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kProjection, false, command_buffer); }

  RecordHeapDispatch(context, command_buffer, *data.project_algorithm, data.project_push, data.splat_count);
  vkexec::barrier::compute_read(command_buffer);

  if (time_pass) { data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kProjection, true, command_buffer); }
}

void RecordPhaseACompute(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
{
  if (context.vkexec_context == nullptr || !data.instance_count_buffer) { return; }

  RecordProjection(context, data, command_buffer, true);

  context.disp.cmdFillBuffer(command_buffer, data.instance_count_buffer->handle(), 0, sizeof(u32), 0U);
  vkexec::barrier::transfer_to_compute(command_buffer);

  if (data.bin_algorithm) {
    ScopedGpuPass const timer{ context, data, command_buffer, GpuPass::kBinning };
    RecordHeapDispatch(context, command_buffer, *data.bin_algorithm, data.bin_push, data.splat_count);
    vkexec::barrier::compute_read(command_buffer);
  }

  if (data.prepare_sort_algorithm) {
    ScopedGpuPass const timer{ context, data, command_buffer, GpuPass::kPrepareSort };
    auto const sort_grid = Dispatch2dForThreads(context, data.gaussian_sort_size, kComputeLocalSizeX);
    (void)vkexec::record_pass(*context.vkexec_context,
      command_buffer,
      data.prepare_sort_algorithm->bind(),
      std::as_bytes(std::span{ &data.sort_push, 1 }),
      sort_grid);
    vkexec::barrier::compute_to_compute(command_buffer);
  }

  if (data.radix_histogram_algorithm && data.radix_scatter_algorithm && data.radix_dispatch_buffer) {
    ScopedGpuPass const timer{ context, data, command_buffer, GpuPass::kRadixSort };

    constexpr u32 kRadixPasses = 4U;
    u64 const instance_count_address = vulkan::DeviceAddressOrZero(data.instance_count_buffer);

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

      RecordHeapIndirect(
        context, command_buffer, *data.radix_histogram_algorithm, radix_push, data.radix_dispatch_buffer->handle());
      vkexec::barrier::compute_to_compute(command_buffer);

      RecordHeapIndirect(
        context, command_buffer, *data.radix_scatter_algorithm, radix_push, data.radix_dispatch_buffer->handle());
      vkexec::barrier::compute_to_compute(command_buffer);
    }
  }
}

void RecordRasterization(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
{
  ScopedGpuPass const timer{ context, data, command_buffer, GpuPass::kRasterize };
  DispatchRasterization(context, data, data.raster_push, command_buffer, data.present_image_index);
}

}// namespace vkgsplat::gs
