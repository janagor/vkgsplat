#include "gs/tiled_compute/resources.hpp"

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan_context.hpp"

#include <algorithm>
#include <expected>
#include <format>
#include <limits>
#include <system_error>
#include <utility>

namespace vkgsplat::gs::tiled {
namespace {

constexpr u64 kRadixBins = 256U;
constexpr u64 kRadixBlockSize = 256U;
constexpr u64 kRadixPrefixBlocksPerGroup = 256U;
constexpr u64 kScanBlockSize = 256U;
constexpr u64 kKeyCopies = 2U;
constexpr u64 kValueCopies = 2U;
constexpr u64 kRadixScratchCopies = 2U;
constexpr u64 kMinimumIntersections = 1U;
constexpr u64 kCountWorkgroupSize = 64U;

[[nodiscard]] auto CeilDiv(u64 numerator, u64 denominator) -> u64
{ return (numerator / denominator) + (numerator % denominator != 0U ? 1U : 0U); }

[[nodiscard]] auto CheckedBytes(u64 elements, u64 stride, u64 limit, char const *name)
  -> std::expected<VkDeviceSize, Error>
{
  if (elements == 0U || elements > std::numeric_limits<VkDeviceSize>::max() / stride
      || elements * stride > limit) {
    return std::unexpected(MakeError(std::errc::value_too_large,
      std::format("tiled {} buffer exceeds maxStorageBufferRange", name)));
  }
  return static_cast<VkDeviceSize>(elements * stride);
}

[[nodiscard]] auto Allocate(vulkan::Context &context, std::optional<GpuBuffer> &destination,
  VkDeviceSize size, char const *name) -> std::expected<void, Error>
{
  auto created = vulkan::CreateDeviceStorageBuffer(*context.allocator, size);
  if (!created) {
    return std::unexpected(MakeError(std::errc::not_enough_memory,
      std::format("failed to allocate tiled {} buffer: {}", name, created.error().message())));
  }
  destination = std::move(*created);
  if (vulkan::DeviceAddressOrZero(destination) == 0U) {
    return std::unexpected(MakeError(std::errc::state_not_recoverable,
      std::format("tiled {} buffer has no device address", name)));
  }
  return {};
}

}// namespace

void DestroyResources(RenderData &data)
{
  data.tiled_counts_buffer.reset();
  data.tiled_offsets_buffer.reset();
  data.tiled_total_buffer.reset();
  data.tiled_overflow_buffer.reset();
  data.tiled_status_pending = false;
  data.tiled_scan_sums.clear();
  data.tiled_scan_offsets.clear();
  data.tiled_radix_prefix_buffer.reset();
  data.tiled_radix_group_buffer.reset();
  data.tiled_radix_prefix_groups = 0U;
  data.tiled_debug_readback_buffer.reset();
  data.tiled_debug_offsets.fill(0U);
  data.tiled_debug_sizes.fill(0U);
  data.unsorted_keys_buffer.reset();
  data.unsorted_values_buffer.reset();
  data.sorted_keys_buffer.reset();
  data.sorted_values_buffer.reset();
  data.sort_histogram_buffer.reset();
  data.radix_dispatch_buffer.reset();
  data.tile_ranges_buffer.reset();
  data.tiled_intersection_capacity = 0U;
  data.tiled_tiles_x = 0U;
  data.tiled_tiles_y = 0U;
  data.tiled_radix_workgroups = 0U;
}

auto InitResources(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  if (context.allocator == nullptr || data.splat_count == 0U || data.output_extent.width == 0U
      || data.output_extent.height == 0U) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid tiled resource context or extent"));
  }
  DestroyResources(data);
  auto const &limits = context.device.physical_device.properties.limits;
  constexpr u64 kIntersectionBytes = kKeyCopies * sizeof(TiledSortKey) + kValueCopies * sizeof(u32);
  constexpr u64 kRadixBytesPerIntersection = kRadixScratchCopies * kRadixBins * sizeof(u32) / kRadixBlockSize;
  u64 const capacity = data.tiled_intersection_budget_bytes / (kIntersectionBytes + kRadixBytesPerIntersection);
  if (capacity < kMinimumIntersections || capacity > std::numeric_limits<u32>::max() / 2U) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid tiled intersection memory budget"));
  }
  u64 const radix_workgroups = CeilDiv(capacity, kRadixBlockSize);
  u64 const prefix_groups = CeilDiv(radix_workgroups, kRadixPrefixBlocksPerGroup);
  u64 const tiles_x = CeilDiv(data.output_extent.width, kTileSize);
  u64 const tiles_y = CeilDiv(data.output_extent.height, kTileSize);
  u64 const tile_count = tiles_x * tiles_y;
  if (tile_count == 0U || tile_count > std::numeric_limits<u32>::max()
      || CeilDiv(data.splat_count, kCountWorkgroupSize) > limits.maxComputeWorkGroupCount[0]
      || prefix_groups > limits.maxComputeWorkGroupCount[0]
      || radix_workgroups > static_cast<u64>(limits.maxComputeWorkGroupCount[0])
                              * limits.maxComputeWorkGroupCount[1]
      || tiles_x > limits.maxComputeWorkGroupCount[0] || tiles_y > limits.maxComputeWorkGroupCount[1]) {
    return std::unexpected(MakeError(std::errc::value_too_large, "tiled dispatch or tile grid exceeds device limits"));
  }

  u64 const max_range = limits.maxStorageBufferRange;
  auto const counts_size = CheckedBytes(data.splat_count, sizeof(u32), max_range, "counts");
  auto const offsets_size = CheckedBytes(static_cast<u64>(data.splat_count) + 1U, sizeof(u32), max_range, "offsets");
  auto const keys_size = CheckedBytes(capacity, sizeof(TiledSortKey), max_range, "keys");
  auto const values_size = CheckedBytes(capacity, sizeof(u32), max_range, "values");
  auto const histogram_size = CheckedBytes(radix_workgroups * kRadixBins, sizeof(u32), max_range, "histogram");
  auto const prefix_group_size = CheckedBytes((prefix_groups + 1U) * kRadixBins,
    sizeof(u32), max_range, "radix prefix groups");
  auto const ranges_size = CheckedBytes(tile_count, sizeof(TileRange), max_range, "tile ranges");
  if (!counts_size) { return std::unexpected(counts_size.error()); }
  if (!offsets_size) { return std::unexpected(offsets_size.error()); }
  if (!keys_size) { return std::unexpected(keys_size.error()); }
  if (!values_size) { return std::unexpected(values_size.error()); }
  if (!histogram_size) { return std::unexpected(histogram_size.error()); }
  if (!prefix_group_size) { return std::unexpected(prefix_group_size.error()); }
  if (!ranges_size) { return std::unexpected(ranges_size.error()); }

  data.tiled_intersection_capacity = static_cast<u32>(capacity);
  data.tiled_radix_workgroups = static_cast<u32>(radix_workgroups);
  data.tiled_radix_prefix_groups = static_cast<u32>(prefix_groups);
  data.tiled_tiles_x = static_cast<u32>(tiles_x);
  data.tiled_tiles_y = static_cast<u32>(tiles_y);
  data.tile_count = static_cast<u32>(tile_count);
  auto allocate = [&](std::optional<GpuBuffer> &buffer, VkDeviceSize size, char const *name)
    -> std::expected<void, Error> { return Allocate(context, buffer, size, name); };

  if (auto result = allocate(data.tiled_counts_buffer, *counts_size, "counts"); !result) { return result; }
  if (auto result = allocate(data.tiled_offsets_buffer, *offsets_size, "offsets"); !result) { return result; }
  auto total = vulkan::CreateStorageBuffer(*context.allocator, sizeof(u32));
  if (!total) { return std::unexpected(total.error()); }
  data.tiled_total_buffer = std::move(*total);
  auto status = vulkan::CreateStorageBuffer(*context.allocator, sizeof(u32));
  if (!status) { return std::unexpected(status.error()); }
  data.tiled_overflow_buffer = std::move(*status);
  if (auto result = allocate(data.sorted_keys_buffer, *keys_size, "keys A"); !result) { return result; }
  if (auto result = allocate(data.unsorted_keys_buffer, *keys_size, "keys B"); !result) { return result; }
  if (auto result = allocate(data.sorted_values_buffer, *values_size, "values A"); !result) { return result; }
  if (auto result = allocate(data.unsorted_values_buffer, *values_size, "values B"); !result) { return result; }
  if (auto result = allocate(data.sort_histogram_buffer, *histogram_size, "histograms"); !result) { return result; }
  if (auto result = allocate(data.tiled_radix_prefix_buffer, *histogram_size, "radix prefixes"); !result) {
    return result;
  }
  if (auto result = allocate(data.tiled_radix_group_buffer, *prefix_group_size, "radix prefix groups"); !result) {
    return result;
  }
  if (auto result = allocate(data.radix_dispatch_buffer, sizeof(VkDispatchIndirectCommand), "indirect dispatch");
    !result) {
    return result;
  }
  if (auto result = allocate(data.tile_ranges_buffer, *ranges_size, "tile ranges"); !result) { return result; }

  u64 level_count = data.splat_count;
  while (level_count > kScanBlockSize) {
    level_count = CeilDiv(level_count, kScanBlockSize);
    auto const level_size = CheckedBytes(level_count, sizeof(u32), max_range, "scan level");
    if (!level_size) { return std::unexpected(level_size.error()); }
    std::optional<GpuBuffer> sums;
    std::optional<GpuBuffer> offsets;
    if (auto result = allocate(sums, *level_size, "scan sums"); !result) { return result; }
    if (auto result = allocate(offsets, *level_size, "scan offsets"); !result) { return result; }
    data.tiled_scan_sums.push_back(std::move(*sums));
    data.tiled_scan_offsets.push_back(std::move(*offsets));
  }
  if (data.enable_tiled_debug_readback) {
    if (!data.projected_buffer) {
      return std::unexpected(MakeError(std::errc::state_not_recoverable, "projected buffer is missing for tiled debug"));
    }
    VkDeviceSize size = 0U;
    auto section = [&](TiledDebugSection selected, VkDeviceSize bytes) -> void {
      size_t const index = static_cast<size_t>(selected);
      data.tiled_debug_offsets.at(index) = size;
      data.tiled_debug_sizes.at(index) = bytes;
      size += bytes;
    };
    section(TiledDebugSection::kProjected, data.projected_buffer->size());
    section(TiledDebugSection::kCounts, data.tiled_counts_buffer->size());
    section(TiledDebugSection::kOffsets, data.tiled_offsets_buffer->size());
    section(TiledDebugSection::kEmittedKeys, data.sorted_keys_buffer->size());
    section(TiledDebugSection::kEmittedIndices, data.sorted_values_buffer->size());
    section(TiledDebugSection::kSortedKeys, data.sorted_keys_buffer->size());
    section(TiledDebugSection::kSortedIndices, data.sorted_values_buffer->size());
    section(TiledDebugSection::kRanges, data.tile_ranges_buffer->size());
    auto readback = vulkan::CreateStagingBuffer(*context.allocator, size);
    if (!readback) { return std::unexpected(readback.error()); }
    data.tiled_debug_readback_buffer = std::move(*readback);
  }
  return {};
}

auto RecreateExtentResources(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{ return InitResources(context, data); }

}// namespace vkgsplat::gs::tiled
