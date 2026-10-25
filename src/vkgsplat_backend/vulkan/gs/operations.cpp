#include "gs/operations.hpp"

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "gs/rasterization.hpp"
#include "gs/tiled_compute/operations.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat_utility/types.hpp>

#include <expected>
#include <print>
#include <span>
#include <system_error>
#include <vkgsplat_utility/error.hpp>

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
  auto RecordHeapDispatch(vulkan::Context const &context,
    VkCommandBuffer command_buffer,
    vkexec::algorithm const &algorithm,
    Params const &params,
    u32 work_count) -> std::expected<void, Error>
  {
    if (work_count == 0) { return {}; }
    auto recorded = vkexec::record_pass(*context.vkexec_context,
      command_buffer,
      algorithm.bind(),
      std::as_bytes(std::span{ &params, 1 }),
      algorithm.groups_for(work_count));
    if (!recorded) { return std::unexpected{ MakeError(std::errc::io_error, recorded.error().message()) }; }
    return {};
  }

  template<typename Params>
  auto RecordHeapIndirect(vulkan::Context const &context,
    VkCommandBuffer command_buffer,
    vkexec::algorithm const &algorithm,
    Params const &params,
    VkBuffer indirect_buffer) -> std::expected<void, Error>
  {
    auto recorded = vkexec::record_pass(*context.vkexec_context,
      command_buffer,
      algorithm.bind(),
      std::as_bytes(std::span{ &params, 1 }),
      vkexec::indirect_dispatch{ .buffer = indirect_buffer, .offset = 0 });
    if (!recorded) { return std::unexpected{ MakeError(std::errc::io_error, recorded.error().message()) }; }
    return {};
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

  [[nodiscard]] auto RecordBinning(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>
  {
    if (!data.bin_algorithm) {
      return std::unexpected{ MakeError(std::errc::state_not_recoverable, "gaussian binning pipeline is not initialized") };
    }
    ScopedGpuPass const timer{ context, data, command_buffer, GpuPass::kBinning };
    if (auto recorded =
          RecordHeapDispatch(context, command_buffer, *data.bin_algorithm, data.bin_push, data.splat_count);
      !recorded) {
      return recorded;
    }
    auto barrier = vkexec::barrier::compute_read(*context.vkexec_context, command_buffer);
    if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }
    return {};
  }

  [[nodiscard]] auto RecordSortPreparation(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>
  {
    if (!data.prepare_sort_algorithm) {
      return std::unexpected{ MakeError(std::errc::state_not_recoverable, "sort preparation pipeline is not initialized") };
    }
    ScopedGpuPass const timer{ context, data, command_buffer, GpuPass::kPrepareSort };
    auto const sort_grid = Dispatch2dForThreads(context, data.gaussian_sort_size, kComputeLocalSizeX);
    auto recorded = vkexec::record_pass(*context.vkexec_context,
      command_buffer,
      data.prepare_sort_algorithm->bind(),
      std::as_bytes(std::span{ &data.sort_push, 1 }),
      sort_grid);
    if (!recorded) { return std::unexpected{ MakeError(std::errc::io_error, recorded.error().message()) }; }
    auto barrier = vkexec::barrier::compute_to_compute(*context.vkexec_context, command_buffer);
    if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }
    return {};
  }

  [[nodiscard]] auto RecordRadixStep(vulkan::Context const &context,
    VkCommandBuffer command_buffer,
    vkexec::algorithm const &algorithm,
    RadixPushConstants const &push,
    VkBuffer indirect_buffer) -> std::expected<void, Error>
  {
    if (auto recorded = RecordHeapIndirect(context, command_buffer, algorithm, push, indirect_buffer); !recorded) {
      return recorded;
    }
    auto barrier = vkexec::barrier::compute_to_compute(*context.vkexec_context, command_buffer);
    if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }
    return {};
  }

  [[nodiscard]] auto RecordRadixSort(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>
  {
    if (!data.radix_histogram_algorithm || !data.radix_scatter_algorithm || !data.radix_dispatch_buffer) {
      return std::unexpected{ MakeError(std::errc::state_not_recoverable, "radix sorting resources are not initialized") };
    }
    ScopedGpuPass const timer{ context, data, command_buffer, GpuPass::kRadixSort };
    constexpr u32 kRadixPasses = 4U;
    constexpr u32 kRadixBitsPerPass = 8U;
    constexpr u32 kRadixPingMask = 1U;
    u64 const instance_count_address = vulkan::DeviceAddressOrZero(data.instance_count_buffer);
    VkBuffer indirect_buffer = data.radix_dispatch_buffer->handle();

    for (u32 pass = 0U; pass < kRadixPasses; ++pass) {
      RadixPushConstants const radix_push{
        .instance_count_address = instance_count_address,
        .capacity = data.gaussian_sort_size,
        .shift = pass * kRadixBitsPerPass,
        .num_blocks_per_workgroup = data.radix_blocks_per_workgroup,
        .ping = pass & kRadixPingMask,
        .pad0 = 0U,
        .pad1 = 0U,
      };
      if (auto recorded =
            RecordRadixStep(context, command_buffer, *data.radix_histogram_algorithm, radix_push, indirect_buffer);
        !recorded) {
        return recorded;
      }
      if (auto recorded =
            RecordRadixStep(context, command_buffer, *data.radix_scatter_algorithm, radix_push, indirect_buffer);
        !recorded) {
        return recorded;
      }
    }
    return {};
  }

}// namespace

auto RecordProjection(vulkan::Context const &context,
  RenderData const &data,
  VkCommandBuffer command_buffer,
  bool time_pass) -> std::expected<void, Error>
{
  if (!data.project_algorithm || context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "projection is not initialized") };
  }

  if (time_pass) { data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kProjection, false, command_buffer); }

  if (auto recorded =
        RecordHeapDispatch(context, command_buffer, *data.project_algorithm, data.project_push, data.splat_count);
    !recorded) {
    return recorded;
  }
  auto barrier = vkexec::barrier::compute_read(*context.vkexec_context, command_buffer);
  if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }

  if (time_pass) { data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kProjection, true, command_buffer); }
  return {};
}

auto RecordPhaseACompute(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
  -> std::expected<void, Error>
{
  if (auto recorded = RecordProjection(context, data, command_buffer, true); !recorded) { return recorded; }

  switch (data.rasterization_mode) {
  case RasterizationMode::kGraphicsPipeline: break;
  case RasterizationMode::kComputeTiled: return tiled::RecordPreparation(context, data, command_buffer);
  default: return std::unexpected{ MakeError(std::errc::invalid_argument, "unknown rasterization mode") };
  }
  if (!data.instance_count_buffer) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "graphics pipeline instance counter is not initialized") };
  }

  context.disp.cmdFillBuffer(command_buffer, data.instance_count_buffer->handle(), 0, sizeof(u32), 0U);
  if (auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer); !barrier) {
    return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) };
  }

  if (auto recorded = RecordBinning(context, data, command_buffer); !recorded) { return recorded; }
  if (auto recorded = RecordSortPreparation(context, data, command_buffer); !recorded) { return recorded; }
  return RecordRadixSort(context, data, command_buffer);
}

auto RecordRasterization(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
  -> std::expected<void, Error>
{
  ScopedGpuPass const timer{ context, data, command_buffer,
    data.rasterization_mode == RasterizationMode::kComputeTiled ? GpuPass::kTiledRasterize : GpuPass::kRasterize };
  if (data.rasterization_mode == RasterizationMode::kComputeTiled) {
    if (auto recorded = tiled::RecordRasterization(context, data, command_buffer); !recorded) { return recorded; }
    return BlitQuiltToSwapchain(context, data, command_buffer, data.present_image_index);
  }
  return DispatchRasterization(context, data, data.raster_push, command_buffer, data.present_image_index);
}

}// namespace vkgsplat::gs
