#include "gs/tiled_compute/operations.hpp"

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/load_heap_pipeline.hpp"
#include "gs/push_constants.hpp"
#include "gs/tiled_compute/resources.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "vulkan_context.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <glm/ext/vector_uint2.hpp>
#include <vkexec/barrier.hpp>
#include <vkexec/pass.hpp>
#include <vkexec_extensions/descriptor_heap/algorithm.hpp>
#include <vkexec_extensions/descriptor_heap/pass.hpp>
#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs::tiled {
namespace {

  constexpr u32 kCountLocalSize = 64U;
  constexpr u32 kScanLocalSize = 256U;
  constexpr u32 kRadixLocalSize = 256U;
  constexpr u32 kRadixPrefixBlocksPerGroup = 256U;
  constexpr u32 kPrefixModeLocal = 0U;
  constexpr u32 kPrefixModeGroups = 1U;
  constexpr u32 kPrefixModeAdd = 2U;
  constexpr u32 kRasterTileSize = 16U;
  constexpr u32 kRadixPasses = sizeof(TiledSortKey) * 8U / 8U;
  constexpr u32 kScanModeScan = 0U;
  constexpr u32 kScanModeAdd = 1U;
  constexpr u32 kCapacityOverflow = 1U << 0U;
  constexpr u32 kCountEmitMismatch = 1U << 1U;
  constexpr u32 kCountOverflow = 1U << 2U;
  constexpr u32 kDispatchOverflow = 1U << 3U;
  constexpr u32 kInvalidTileId = 1U << 4U;
  constexpr u32 kInvalidRasterMetadata = 1U << 5U;

  struct CountPush
  {
    u64 counts_address{};
    glm::uvec2 viewport{};
    u32 splat_count{};
    u32 tile_size{};
    u32 tiles_x{};
    u32 tiles_y{};
  };

  struct ScanPush
  {
    u64 input_address{};
    u64 output_address{};
    u64 sums_address{};
    u64 block_offsets_address{};
    u64 overflow_address{};
    u32 element_count{};
    u32 mode{};
  };

  struct PreparePush
  {
    u64 counts_address{};
    u64 offsets_address{};
    u64 total_address{};
    u64 overflow_address{};
    u64 dispatch_address{};
    u32 splat_count{};
    u32 capacity{};
    u32 max_groups_x{};
    u32 max_groups_y{};
  };

  struct EmitPush
  {
    u64 counts_address{};
    u64 offsets_address{};
    u64 overflow_address{};
    glm::uvec2 viewport{};
    u32 splat_count{};
    u32 capacity{};
    u32 tile_size{};
    u32 tiles_x{};
    u32 tiles_y{};
  };

  struct RadixPush
  {
    u64 total_address{};
    u64 prefix_address{};
    u64 prefix_group_address{};
    u32 capacity{};
    u32 pass_index{};
    u32 group_count_x{};
    u32 prefix_group_capacity{};
    u32 prefix_mode{};
  };

  struct RangePush
  {
    u64 total_address{};
    u64 overflow_address{};
    u32 capacity{};
    u32 tile_count{};
    u32 group_count_x{};
  };

  struct TiledRasterPush
  {
    RasterPushConstants raster{};
    u64 overflow_address{};
  };

  static_assert(offsetof(TiledRasterPush, overflow_address) == sizeof(RasterPushConstants));

  [[nodiscard]] auto Groups(u32 elements, u32 local_size) -> u32
  { return (elements / local_size) + (elements % local_size != 0U ? 1U : 0U); }

  [[nodiscard]] auto ComputeBarrier(vulkan::Context const &context, VkCommandBuffer command_buffer)
    -> std::expected<void, Error>
  {
    auto barrier = vkexec::barrier::compute_to_compute(*context.vkexec_context, command_buffer);
    if (!barrier) { return std::unexpected(MakeError(std::errc::io_error, barrier.error().message())); }
    return {};
  }

  template<typename Push>
  [[nodiscard]] auto Dispatch(vulkan::Context const &context,
    VkCommandBuffer command_buffer,
    vkexec::algorithm const &algorithm,
    Push const &push,
    vkexec::dispatch groups) -> std::expected<void, Error>
  {
    auto recorded = vkexec::record_pass(
      *context.vkexec_context, command_buffer, algorithm.bind(), std::as_bytes(std::span{ &push, 1 }), groups);
    if (!recorded) { return std::unexpected(MakeError(std::errc::io_error, recorded.error().message())); }
    return {};
  }

  template<typename Push>
  [[nodiscard]] auto DispatchIndirect(vulkan::Context const &context,
    VkCommandBuffer command_buffer,
    vkexec::algorithm const &algorithm,
    Push const &push,
    VkBuffer indirect) -> std::expected<void, Error>
  {
    auto recorded = vkexec::record_pass(*context.vkexec_context,
      command_buffer,
      algorithm.bind(),
      std::as_bytes(std::span{ &push, 1 }),
      vkexec::indirect_dispatch{ .buffer = indirect, .offset = 0U });
    if (!recorded) { return std::unexpected(MakeError(std::errc::io_error, recorded.error().message())); }
    return {};
  }

  [[nodiscard]] auto RecordScan(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
    -> std::expected<void, Error>
  {
    if (!data.tiled_scan_algorithm) {
      return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled scan algorithm is missing"));
    }
    u32 count = data.splat_count;
    u64 input = vulkan::DeviceAddressOrZero(data.tiled_counts_buffer);
    u64 output = vulkan::DeviceAddressOrZero(data.tiled_offsets_buffer);
    u64 const overflow = vulkan::DeviceAddressOrZero(data.tiled_overflow_buffer);
    for (size_t level = 0U;; ++level) {
      u64 const sums =
        level < data.tiled_scan_sums.size() ? vulkan::DeviceAddressOrZero(data.tiled_scan_sums.at(level)) : 0U;
      ScanPush const push{ .input_address = input,
        .output_address = output,
        .sums_address = sums,
        .overflow_address = overflow,
        .element_count = count,
        .mode = kScanModeScan };
      if (auto result = Dispatch(context,
            command_buffer,
            *data.tiled_scan_algorithm,
            push,
            vkexec::dispatch{ .x = Groups(count, kScanLocalSize), .y = 1U, .z = 1U });
        !result) {
        return result;
      }
      if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
      if (level == data.tiled_scan_sums.size()) { break; }
      count = Groups(count, kScanLocalSize);
      input = vulkan::DeviceAddressOrZero(data.tiled_scan_sums.at(level));
      output = vulkan::DeviceAddressOrZero(data.tiled_scan_offsets.at(level));
    }
    for (size_t level = data.tiled_scan_sums.size(); level > 0U; --level) {
      size_t const child = level - 1U;
      u32 child_count = data.splat_count;
      for (size_t index = 0U; index < child; ++index) { child_count = Groups(child_count, kScanLocalSize); }
      u64 const child_output = child == 0U ? vulkan::DeviceAddressOrZero(data.tiled_offsets_buffer)
                                           : vulkan::DeviceAddressOrZero(data.tiled_scan_offsets.at(child - 1U));
      u64 const parent_offsets = vulkan::DeviceAddressOrZero(data.tiled_scan_offsets.at(child));
      ScanPush const push{ .output_address = child_output,
        .block_offsets_address = parent_offsets,
        .overflow_address = overflow,
        .element_count = child_count,
        .mode = kScanModeAdd };
      if (auto result = Dispatch(context,
            command_buffer,
            *data.tiled_scan_algorithm,
            push,
            vkexec::dispatch{ .x = Groups(child_count, kScanLocalSize), .y = 1U, .z = 1U });
        !result) {
        return result;
      }
      if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
    }
    return {};
  }

  [[nodiscard]] auto DebugCopyBarrier(vulkan::Context const &context, VkCommandBuffer command_buffer)
    -> std::expected<void, Error>
  {
    // One dependency covers all copy sources, including device-address shader writes.
    auto barrier = vkexec::memory_barrier(*context.vkexec_context,
      command_buffer,
      { .src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT,
        .src_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dst_access = VK_ACCESS_2_TRANSFER_READ_BIT });
    if (!barrier) { return std::unexpected(MakeError(std::errc::io_error, barrier.error().message())); }
    return {};
  }

  void CopyDebugSection(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer,
    TiledDebugSection section,
    GpuBuffer const &source,
    GpuBuffer const &readback)
  {
    auto const index = static_cast<size_t>(section);
    VkBufferCopy const region{
      .srcOffset = 0U, .dstOffset = data.tiled_debug_offsets.at(index), .size = data.tiled_debug_sizes.at(index)
    };
    context.disp.cmdCopyBuffer(command_buffer, source.handle(), readback.handle(), 1U, &region);
  }

  [[nodiscard]] auto FinishDebugCopies(vulkan::Context const &context, VkCommandBuffer command_buffer)
    -> std::expected<void, Error>
  {
    auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer);
    if (!barrier) { return std::unexpected(MakeError(std::errc::io_error, barrier.error().message())); }
    return {};
  }

  [[nodiscard]] auto Load(vulkan::Context &context,
    std::optional<vkexec::algorithm> &destination,
    char const *filename,
    std::array<u32, 3> local_size,
    std::span<u32 const> specialization = {}) -> std::expected<void, Error>
  {
    auto algorithm =
      LoadHeapAlgorithm(context, std::string(kShaderDirectory) + "/" + filename, specialization, local_size);
    if (!algorithm) {
      return std::unexpected(MakeError(std::errc::io_error, std::string("failed to load tiled shader ") + filename));
    }
    destination = std::move(*algorithm);
    return {};
  }

}// namespace

void DestroyAlgorithms(RenderData &data)
{
  data.tiled_count_algorithm.reset();
  data.tiled_scan_algorithm.reset();
  data.tiled_emit_algorithm.reset();
  data.tiled_prepare_algorithm.reset();
  data.tiled_histogram_algorithm.reset();
  data.tiled_prefix_algorithm.reset();
  data.tiled_scatter_algorithm.reset();
  data.tiled_ranges_algorithm.reset();
  data.tiled_raster_algorithm.reset();
}

auto InitAlgorithms(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  DestroyAlgorithms(data);
  std::array<u32, 1> const raster_capacity{ data.tiled_intersection_capacity };
  if (auto result = Load(context, data.tiled_count_algorithm, "tiled_count.comp.spv", { kCountLocalSize, 1U, 1U });
    !result) {
    return result;
  }
  if (auto result = Load(context, data.tiled_scan_algorithm, "tiled_scan.comp.spv", { kScanLocalSize, 1U, 1U });
    !result) {
    return result;
  }
  if (auto result = Load(context, data.tiled_emit_algorithm, "tiled_emit.comp.spv", { kCountLocalSize, 1U, 1U });
    !result) {
    return result;
  }
  if (auto result = Load(context, data.tiled_prepare_algorithm, "tiled_prepare.comp.spv", { 1U, 1U, 1U }); !result) {
    return result;
  }
  if (auto result =
        Load(context, data.tiled_histogram_algorithm, "tiled_radix_histograms.comp.spv", { kRadixLocalSize, 1U, 1U });
    !result) {
    return result;
  }
  if (auto result =
        Load(context, data.tiled_prefix_algorithm, "tiled_radix_prefix.comp.spv", { kRadixLocalSize, 1U, 1U });
    !result) {
    return result;
  }
  if (auto result =
        Load(context, data.tiled_scatter_algorithm, "tiled_radix_scatter.comp.spv", { kRadixLocalSize, 1U, 1U });
    !result) {
    return result;
  }
  if (auto result =
        Load(context, data.tiled_ranges_algorithm, "tiled_identify_ranges.comp.spv", { kRadixLocalSize, 1U, 1U });
    !result) {
    return result;
  }
  if (auto result = Load(context,
        data.tiled_raster_algorithm,
        "tiled_rasterization.comp.spv",
        { kRasterTileSize, kRasterTileSize, 1U },
        raster_capacity);
    !result) {
    return result;
  }
  return {};
}

namespace {

  [[nodiscard]] auto RecordCountScanEmit(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>
  {
    if (!data.tiled_count_algorithm || !data.tiled_scan_algorithm || !data.tiled_emit_algorithm
        || !data.tiled_prepare_algorithm || !data.tiled_histogram_algorithm || !data.tiled_prefix_algorithm
        || !data.tiled_scatter_algorithm || !data.tiled_ranges_algorithm || !data.tiled_overflow_buffer
        || !data.tile_ranges_buffer || !data.radix_dispatch_buffer || !data.tiled_radix_group_buffer) {
      return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled preparation resources are missing"));
    }
    context.disp.cmdFillBuffer(command_buffer, data.tiled_overflow_buffer->handle(), 0U, sizeof(u32), 0U);
    if (auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer); !barrier) {
      return std::unexpected(MakeError(std::errc::io_error, barrier.error().message()));
    }
    glm::uvec2 const viewport{ data.output_extent.width, data.output_extent.height };
    CountPush const count_push{ .counts_address = vulkan::DeviceAddressOrZero(data.tiled_counts_buffer),
      .viewport = viewport,
      .splat_count = data.splat_count,
      .tile_size = kTileSize,
      .tiles_x = data.tiled_tiles_x,
      .tiles_y = data.tiled_tiles_y };
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledCount, false, command_buffer);
    if (auto result = Dispatch(context,
          command_buffer,
          *data.tiled_count_algorithm,
          count_push,
          vkexec::dispatch{ .x = Groups(data.splat_count, kCountLocalSize), .y = 1U, .z = 1U });
      !result) {
      return result;
    }
    if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledCount, true, command_buffer);
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledScan, false, command_buffer);
    if (auto result = RecordScan(context, data, command_buffer); !result) { return result; }

    auto const &limits = context.device.physical_device.properties.limits;
    PreparePush const prepare_push{
      .counts_address = vulkan::DeviceAddressOrZero(data.tiled_counts_buffer),
      .offsets_address = vulkan::DeviceAddressOrZero(data.tiled_offsets_buffer),
      .total_address = vulkan::DeviceAddressOrZero(data.tiled_total_buffer),
      .overflow_address = vulkan::DeviceAddressOrZero(data.tiled_overflow_buffer),
      .dispatch_address = vulkan::DeviceAddressOrZero(data.radix_dispatch_buffer),
      .splat_count = data.splat_count,
      .capacity = data.tiled_intersection_capacity,
      .max_groups_x = limits.maxComputeWorkGroupCount[0],
      .max_groups_y = limits.maxComputeWorkGroupCount[1],
    };
    if (auto result = Dispatch(context,
          command_buffer,
          *data.tiled_prepare_algorithm,
          prepare_push,
          vkexec::dispatch{ .x = 1U, .y = 1U, .z = 1U });
      !result) {
      return result;
    }
    if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
    // ComputeBarrier also covers DRAW_INDIRECT / INDIRECT_COMMAND_READ in pinned vkexec.
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledScan, true, command_buffer);

    EmitPush const emit_push{ .counts_address = prepare_push.counts_address,
      .offsets_address = prepare_push.offsets_address,
      .overflow_address = prepare_push.overflow_address,
      .viewport = viewport,
      .splat_count = data.splat_count,
      .capacity = data.tiled_intersection_capacity,
      .tile_size = kTileSize,
      .tiles_x = data.tiled_tiles_x,
      .tiles_y = data.tiled_tiles_y };
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledEmit, false, command_buffer);
    if (auto result = Dispatch(context,
          command_buffer,
          *data.tiled_emit_algorithm,
          emit_push,
          vkexec::dispatch{ .x = Groups(data.splat_count, kCountLocalSize), .y = 1U, .z = 1U });
      !result) {
      return result;
    }
    if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledEmit, true, command_buffer);
    if (data.enable_tiled_debug_readback) {
      if (!data.tiled_debug_readback_buffer || !data.sorted_keys_buffer || !data.sorted_values_buffer) {
        return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled debug resources are missing"));
      }
      if (auto result = DebugCopyBarrier(context, command_buffer); !result) { return result; }
      CopyDebugSection(context,
        data,
        command_buffer,
        TiledDebugSection::kEmittedKeys,
        *data.sorted_keys_buffer,
        *data.tiled_debug_readback_buffer);
      CopyDebugSection(context,
        data,
        command_buffer,
        TiledDebugSection::kEmittedIndices,
        *data.sorted_values_buffer,
        *data.tiled_debug_readback_buffer);
      if (auto result = FinishDebugCopies(context, command_buffer); !result) { return result; }
    }

    return {};
  }

  [[nodiscard]] auto RecordRadixSort(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>
  {
    if (!data.radix_dispatch_buffer || !data.tiled_histogram_algorithm || !data.tiled_prefix_algorithm
        || !data.tiled_scatter_algorithm) {
      return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled radix resources are missing"));
    }
    VkBuffer indirect = data.radix_dispatch_buffer->handle();
    auto const &limits = context.device.physical_device.properties.limits;
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledRadixSort, false, command_buffer);
    for (u32 pass = 0U; pass < kRadixPasses; ++pass) {
      RadixPush const radix_push{ .total_address = vulkan::DeviceAddressOrZero(data.tiled_total_buffer),
        .prefix_address = vulkan::DeviceAddressOrZero(data.tiled_radix_prefix_buffer),
        .prefix_group_address = vulkan::DeviceAddressOrZero(data.tiled_radix_group_buffer),
        .capacity = data.tiled_intersection_capacity,
        .pass_index = pass,
        .group_count_x = limits.maxComputeWorkGroupCount[0],
        .prefix_group_capacity = data.tiled_radix_prefix_groups };
      if (auto result =
            DispatchIndirect(context, command_buffer, *data.tiled_histogram_algorithm, radix_push, indirect);
        !result) {
        return result;
      }
      if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
      for (u32 const prefix_mode : { kPrefixModeLocal, kPrefixModeGroups, kPrefixModeAdd }) {
        RadixPush prefix_push = radix_push;
        prefix_push.prefix_mode = prefix_mode;
        u32 const groups =
          prefix_mode == kPrefixModeGroups ? 1U : Groups(data.tiled_radix_workgroups, kRadixPrefixBlocksPerGroup);
        if (auto result = Dispatch(context,
              command_buffer,
              *data.tiled_prefix_algorithm,
              prefix_push,
              vkexec::dispatch{ .x = groups, .y = 1U, .z = 1U });
          !result) {
          return result;
        }
        if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
      }
      if (auto result = DispatchIndirect(context, command_buffer, *data.tiled_scatter_algorithm, radix_push, indirect);
        !result) {
        return result;
      }
      if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
    }
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledRadixSort, true, command_buffer);

    return {};
  }

  [[nodiscard]] auto RecordRanges(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>
  {
    if (!data.tile_ranges_buffer || !data.tiled_ranges_algorithm || !data.radix_dispatch_buffer) {
      return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled range resources are missing"));
    }
    VkBuffer indirect = data.radix_dispatch_buffer->handle();
    auto const &limits = context.device.physical_device.properties.limits;
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledRanges, false, command_buffer);
    context.disp.cmdFillBuffer(command_buffer,
      data.tile_ranges_buffer->handle(),
      0U,
      static_cast<VkDeviceSize>(data.tile_count) * sizeof(TileRange),
      0U);
    if (auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer); !barrier) {
      return std::unexpected(MakeError(std::errc::io_error, barrier.error().message()));
    }
    RangePush const range_push{ .total_address = vulkan::DeviceAddressOrZero(data.tiled_total_buffer),
      .overflow_address = vulkan::DeviceAddressOrZero(data.tiled_overflow_buffer),
      .capacity = data.tiled_intersection_capacity,
      .tile_count = data.tile_count,
      .group_count_x = limits.maxComputeWorkGroupCount[0] };
    if (auto result = DispatchIndirect(context, command_buffer, *data.tiled_ranges_algorithm, range_push, indirect);
      !result) {
      return result;
    }
    if (auto result = ComputeBarrier(context, command_buffer); !result) { return result; }
    data.gpu_pass_timer.write(context, data.current_slot, GpuPass::kTiledRanges, true, command_buffer);
    if (data.enable_tiled_debug_readback) {
      if (!data.tiled_debug_readback_buffer || !data.projected_buffer || !data.tiled_counts_buffer
          || !data.tiled_offsets_buffer || !data.sorted_keys_buffer || !data.sorted_values_buffer) {
        return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled debug resources are missing"));
      }
      if (auto result = DebugCopyBarrier(context, command_buffer); !result) { return result; }
      CopyDebugSection(context,
        data,
        command_buffer,
        TiledDebugSection::kProjected,
        *data.projected_buffer,
        *data.tiled_debug_readback_buffer);
      CopyDebugSection(context,
        data,
        command_buffer,
        TiledDebugSection::kCounts,
        *data.tiled_counts_buffer,
        *data.tiled_debug_readback_buffer);
      CopyDebugSection(context,
        data,
        command_buffer,
        TiledDebugSection::kOffsets,
        *data.tiled_offsets_buffer,
        *data.tiled_debug_readback_buffer);
      CopyDebugSection(context,
        data,
        command_buffer,
        TiledDebugSection::kSortedKeys,
        *data.sorted_keys_buffer,
        *data.tiled_debug_readback_buffer);
      CopyDebugSection(context,
        data,
        command_buffer,
        TiledDebugSection::kSortedIndices,
        *data.sorted_values_buffer,
        *data.tiled_debug_readback_buffer);
      CopyDebugSection(context,
        data,
        command_buffer,
        TiledDebugSection::kRanges,
        *data.tile_ranges_buffer,
        *data.tiled_debug_readback_buffer);
      if (auto result = FinishDebugCopies(context, command_buffer); !result) { return result; }
    }
    return {};
  }

}// namespace

auto RecordPreparation(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
  -> std::expected<void, Error>
{
  if (auto result = RecordCountScanEmit(context, data, command_buffer); !result) { return result; }
  if (auto result = RecordRadixSort(context, data, command_buffer); !result) { return result; }
  return RecordRanges(context, data, command_buffer);
}

auto RecordRasterization(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
  -> std::expected<void, Error>
{
  if (!data.tiled_raster_algorithm || !data.color_image || !data.tiled_overflow_buffer) {
    return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled rasterization resources are missing"));
  }
  auto before = vkexec::image_barrier(*context.vkexec_context,
    command_buffer,
    { .image = data.color_image->handle(),
      .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
      .new_layout = VK_IMAGE_LAYOUT_GENERAL,
      .src_stage = VK_PIPELINE_STAGE_2_NONE,
      .dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .src_access = VK_ACCESS_2_NONE,
      .dst_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT });
  if (!before) { return std::unexpected(MakeError(std::errc::io_error, before.error().message())); }
  TiledRasterPush const push{ .raster = data.raster_push,
    .overflow_address = vulkan::DeviceAddressOrZero(data.tiled_overflow_buffer) };
  if (auto result = Dispatch(context,
        command_buffer,
        *data.tiled_raster_algorithm,
        push,
        vkexec::dispatch{ .x = data.tiled_tiles_x, .y = data.tiled_tiles_y, .z = 1U });
    !result) {
    return result;
  }
  auto after = vkexec::image_barrier(*context.vkexec_context,
    command_buffer,
    { .image = data.color_image->handle(),
      .old_layout = VK_IMAGE_LAYOUT_GENERAL,
      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
      .src_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
      .dst_access = VK_ACCESS_2_TRANSFER_READ_BIT });
  if (!after) { return std::unexpected(MakeError(std::errc::io_error, after.error().message())); }
  return {};
}

auto DecodeFrameStatus(u32 status) -> std::expected<void, Error>
{
  if (status == 0U) { return {}; }
  char const *reason = "unknown tiled GPU error";
  if ((status & kCapacityOverflow) != 0U) {
    reason = "intersection capacity exceeded";
  } else if ((status & kCountEmitMismatch) != 0U) {
    reason = "tile count and emission disagree";
  } else if ((status & kCountOverflow) != 0U) {
    reason = "intersection count exceeded 32 bits";
  } else if ((status & kDispatchOverflow) != 0U) {
    reason = "radix dispatch exceeds device limits";
  } else if ((status & kInvalidTileId) != 0U) {
    reason = "invalid tile ID";
  } else if ((status & kInvalidRasterMetadata) != 0U) {
    reason = "invalid rasterizer metadata";
  }
  return std::unexpected(MakeError(
    std::errc::result_out_of_range, std::format("tiled GPU frame failed: {} (status bits 0x{:x})", reason, status)));
}

auto ReadFrameStatus(RenderData const &data) -> std::expected<void, Error>
{
  if (!data.tiled_overflow_buffer) {
    return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled status buffer is missing"));
  }
  auto const invalidated = data.tiled_overflow_buffer->invalidate();
  if (!invalidated) { return std::unexpected(MakeError(std::errc::io_error, invalidated.error().message())); }
  auto const bytes = data.tiled_overflow_buffer->mapped();
  if (bytes.size() < sizeof(u32)) {
    return std::unexpected(MakeError(std::errc::state_not_recoverable, "tiled status buffer is not mapped"));
  }
  u32 status{};
  std::memcpy(&status, bytes.data(), sizeof(status));
  return DecodeFrameStatus(status);
}

}// namespace vkgsplat::gs::tiled
