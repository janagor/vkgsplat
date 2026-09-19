#include "vulkan/descriptor/descriptor_heap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <utility>

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vkexec/context.hpp>
#include <vkexec/descriptor_schema.hpp>
#include <vkexec/pipeline.hpp>
#include <vkexec/resource_table.hpp>
#include <vkexec_extensions/descriptor_heap/descriptor_heap.hpp>
#include <vkexec_extensions/descriptor_heap/resource_table.hpp>
#include <vkexec_extensions/descriptor_heap/strategy.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {
namespace {

  [[nodiscard]] auto RequireVkexec(vulkan::Context const &context) -> vkexec::context *
  {
    if (context.vkexec_context == nullptr) {
      std::println("vkexec context missing for descriptor heap path");
      return nullptr;
    }
    return context.vkexec_context.get();
  }

  template<HeapSlot Slot> using GsStorageBuffer = vkexec::storage_buffer<static_cast<u32>(Slot)>;

  using GsHeapSchema = vkexec::descriptor_schema<GsStorageBuffer<HeapSlot::kGeometry>,
    GsStorageBuffer<HeapSlot::kAppearance>,
    GsStorageBuffer<HeapSlot::kSortedIndices>,
    GsStorageBuffer<HeapSlot::kSortEntries>,
    GsStorageBuffer<HeapSlot::kProjected>,
    GsStorageBuffer<HeapSlot::kUnsortedKeys>,
    GsStorageBuffer<HeapSlot::kUnsortedValues>,
    GsStorageBuffer<HeapSlot::kSortedKeys>,
    GsStorageBuffer<HeapSlot::kSortedValues>,
    GsStorageBuffer<HeapSlot::kTileRanges>,
    GsStorageBuffer<HeapSlot::kSortHistogram>>;

  inline constexpr GsHeapSchema kGsHeapSchema{};
  inline constexpr std::array<u32, GsHeapSchema::binding_count> kGsPhysicalIndices{
    static_cast<u32>(HeapSlot::kGeometry),
    static_cast<u32>(HeapSlot::kAppearance),
    static_cast<u32>(HeapSlot::kSortedIndices),
    static_cast<u32>(HeapSlot::kSortEntries),
    static_cast<u32>(HeapSlot::kProjected),
    static_cast<u32>(HeapSlot::kUnsortedKeys),
    static_cast<u32>(HeapSlot::kUnsortedValues),
    static_cast<u32>(HeapSlot::kSortedKeys),
    static_cast<u32>(HeapSlot::kSortedValues),
    static_cast<u32>(HeapSlot::kTileRanges),
    static_cast<u32>(HeapSlot::kSortHistogram),
  };

}// namespace

auto QueryDescriptorHeapLayout(vulkan::Context const &context, RenderData &data) -> bool
{
  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr) { return false; }

  auto const layout = vkexec::query_descriptor_heap_layout(*vkexec);
  if (!layout) {
    std::println("query_descriptor_heap_layout failed: {}", layout.error().message());
    return false;
  }

  data.buffer_descriptor_size = layout->buffer_descriptor_size;
  data.image_descriptor_size = layout->image_descriptor_size;
  data.sampler_descriptor_size = layout->sampler_descriptor_size;
  data.descriptor_stride = layout->descriptor_stride;
  data.descriptor_heap_size = vkexec::descriptor_heap_byte_size(*layout, kSharedResourceSlots);
  auto const descriptor_region =
    static_cast<VkDeviceSize>(data.descriptor_stride) * static_cast<VkDeviceSize>(kSharedResourceSlots);
  data.reserved_range_offset = AlignUp(descriptor_region, layout->resource_heap_alignment);
  data.reserved_range_size = layout->min_resource_heap_reserved_range;
  data.sampler_heap_size = vkexec::sampler_heap_byte_size(*layout, kSharedSamplerSlots);
  auto const sampler_region =
    static_cast<VkDeviceSize>(data.sampler_descriptor_size) * static_cast<VkDeviceSize>(kSharedSamplerSlots);
  data.sampler_reserved_range_offset = AlignUp(sampler_region, layout->sampler_heap_alignment);
  data.sampler_reserved_range_size = layout->min_sampler_heap_reserved_range;
  return data.descriptor_stride > 0 && data.sampler_descriptor_size > 0;
}

void DestroyDescriptorHeap(vulkan::Context &context, RenderData &data)
{
  (void)context;
  data.descriptor_heap_buffer.reset();
  data.sampler_heap_buffer.reset();
}

auto RefreshDescriptorHeap(vulkan::Context &context, RenderData &data) -> bool
{
  if (!QueryDescriptorHeapLayout(context, data)) { return false; }

  if (!data.geometry_buffer || !data.appearance_buffer || !data.projected_buffer || !data.unsorted_keys_buffer
      || !data.unsorted_values_buffer || !data.sorted_keys_buffer || !data.sorted_values_buffer
      || !data.sort_histogram_buffer || !data.tile_ranges_buffer || !data.sorted_indices
      || data.sorted_indices->size() == 0 || !data.sort_entries || data.sort_entries->size() == 0) {
    return true;
  }

  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr) { return false; }

  bool const needs_reallocation = !data.descriptor_heap_buffer || !data.sampler_heap_buffer
                                  || data.descriptor_heap_buffer->size() != data.descriptor_heap_size
                                  || data.sampler_heap_buffer->size() != data.sampler_heap_size;
  if (needs_reallocation) {
    // Replacing a bound heap requires all queued frames to be drained first. Once ImGui
    // shares these allocations, its registered descriptors must be replayed and its
    // secondary command buffers invalidated after installing the new device addresses.
    if (data.imgui != nullptr) { context.disp.deviceWaitIdle(); }

    auto resource_heap = vulkan::CreateDescriptorHeapBuffer(*vkexec, data.descriptor_heap_size);
    if (!resource_heap) {
      std::println("Failed to create shared resource heap buffer!");
      return false;
    }
    auto sampler_heap = vulkan::CreateDescriptorHeapBuffer(*vkexec, data.sampler_heap_size);
    if (!sampler_heap) {
      std::println("Failed to create shared sampler heap buffer!");
      return false;
    }
    data.descriptor_heap_buffer = std::move(*resource_heap);
    data.sampler_heap_buffer = std::move(*sampler_heap);
    if (!RefreshImguiSharedHeapBindings(data)) {
      std::println("Failed to replay ImGui descriptors after shared heap reallocation!");
      return false;
    }
  }

  auto const geometry_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianGeometry));
  auto const appearance_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianAppearance));
  auto const projected_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianProjected));
  auto const unsorted_keys_buffer_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(gs::BinningKey));
  auto const unsorted_values_buffer_size = static_cast<VkDeviceSize>(data.max_bin_instances * sizeof(u32));
  auto const sorted_keys_buffer_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(gs::BinningKey));
  auto const sorted_values_buffer_size = static_cast<VkDeviceSize>(data.gaussian_sort_size * sizeof(u32));
  auto const sort_histogram_buffer_size =
    static_cast<VkDeviceSize>(static_cast<size_t>(data.radix_num_workgroups) * 256U * sizeof(u32));
  auto const tile_ranges_buffer_size = static_cast<VkDeviceSize>(data.tile_count * sizeof(gs::TileRange));
  auto const sorted_indices_buffer_size = data.sorted_indices->byte_size();
  auto const sort_entries_buffer_size = data.sort_entries->byte_size();

  auto const resources = vkexec::make_resource_table(kGsHeapSchema,
    vkexec::buffer_resource(data.geometry_buffer->handle(), geometry_buffer_size),
    vkexec::buffer_resource(data.appearance_buffer->handle(), appearance_buffer_size),
    vkexec::buffer_resource(data.sorted_indices->vk_buffer(), sorted_indices_buffer_size),
    vkexec::buffer_resource(data.sort_entries->vk_buffer(), sort_entries_buffer_size),
    vkexec::buffer_resource(data.projected_buffer->handle(), projected_buffer_size),
    vkexec::buffer_resource(data.unsorted_keys_buffer->handle(), unsorted_keys_buffer_size),
    vkexec::buffer_resource(data.unsorted_values_buffer->handle(), unsorted_values_buffer_size),
    vkexec::buffer_resource(data.sorted_keys_buffer->handle(), sorted_keys_buffer_size),
    vkexec::buffer_resource(data.sorted_values_buffer->handle(), sorted_values_buffer_size),
    vkexec::buffer_resource(data.tile_ranges_buffer->handle(), tile_ranges_buffer_size),
    vkexec::buffer_resource(data.sort_histogram_buffer->handle(), sort_histogram_buffer_size));

  auto heap_bytes = data.descriptor_heap_buffer->mapped();
  vkexec::heap_table_lower_env const lower_env{
    .resource_heap_bytes = heap_bytes,
    .sampler_heap_bytes = {},
    .buffer_descriptor_size = data.buffer_descriptor_size,
    .image_descriptor_size = data.image_descriptor_size,
    .descriptor_stride = data.descriptor_stride,
    .sampler_descriptor_size = 0,
    .sampler_descriptor_stride = 0,
    .indices = kGsPhysicalIndices,
    .sampler_indices = {},
    .image_view_infos = {},
    .sampler_infos = {},
  };

  // Heap table lowering does not depend on a pipeline; the backend-neutral
  // interface retains this argument for descriptor-set implementations.
  vkexec::pipeline_resources const unused_pipeline{};
  auto const lowered = vkexec::detail::heap_descriptor_backend::lower(*vkexec, unused_pipeline, resources, lower_env);
  if (!lowered) {
    std::println("Failed to lower GS resource table: {}", lowered.error().message());
    return false;
  }
  if (auto const flushed = data.descriptor_heap_buffer->flush(); !flushed) {
    std::println("Failed to flush GS descriptor heap: {}", flushed.error().message());
    return false;
  }
  if (auto const flushed = data.sampler_heap_buffer->flush(); !flushed) {
    std::println("Failed to flush shared sampler heap: {}", flushed.error().message());
    return false;
  }
  return true;
}

void BindDescriptorHeap(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
{
  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr || !data.descriptor_heap_buffer || !data.sampler_heap_buffer) { return; }

  VkDeviceAddress const heap_address = vulkan::DeviceAddressOrZero(*data.descriptor_heap_buffer);
  auto const bound = vkexec::cmd_bind_resource_heap(*vkexec,
    command_buffer,
    heap_address,
    data.descriptor_heap_size,
    data.reserved_range_offset,
    data.reserved_range_size);
  if (!bound) { std::println("cmd_bind_resource_heap failed: {}", bound.error().message()); }

  VkDeviceAddress const sampler_heap_address = vulkan::DeviceAddressOrZero(*data.sampler_heap_buffer);
  auto const sampler_bound = vkexec::cmd_bind_sampler_heap(*vkexec,
    command_buffer,
    sampler_heap_address,
    data.sampler_heap_size,
    data.sampler_reserved_range_offset,
    data.sampler_reserved_range_size);
  if (!sampler_bound) { std::println("cmd_bind_sampler_heap failed: {}", sampler_bound.error().message()); }
}

auto HeapSlotByteOffset(RenderData const &data, HeapSlot slot) -> uint32_t
{ return static_cast<uint32_t>(static_cast<size_t>(slot) * data.descriptor_stride); }

}// namespace vkgsplat
