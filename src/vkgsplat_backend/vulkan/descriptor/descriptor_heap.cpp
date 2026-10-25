#include "vulkan/descriptor/descriptor_heap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <print>
#include <span>
#include <utility>

#include "app_state.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vkexec/context.hpp>
#include <vkexec/descriptor_schema.hpp>
#include <vkexec/pipeline.hpp>
#include <vkexec/resource_table.hpp>
#include <vkexec_extensions/descriptor_heap/descriptor_heap.hpp>
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
    vkexec::storage_image<static_cast<u32>(HeapSlot::kColorTarget)>,
    GsStorageBuffer<HeapSlot::kSortHistogram>,
    GsStorageBuffer<HeapSlot::kProjectionCameras>>;

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
    static_cast<u32>(HeapSlot::kColorTarget),
    static_cast<u32>(HeapSlot::kSortHistogram),
    static_cast<u32>(HeapSlot::kProjectionCameras),
  };

  [[nodiscard]] auto HasGsBuffers(RenderData const &data) -> bool
  {
    bool const common = data.geometry_buffer && data.appearance_buffer && data.projected_buffer
                        && data.projection_cameras_buffer && data.color_image && data.color_image_view
                        && data.unsorted_keys_buffer && data.unsorted_values_buffer && data.sorted_keys_buffer
                        && data.sorted_values_buffer && data.sort_histogram_buffer && data.tile_ranges_buffer
                        && data.sorted_indices && data.sorted_indices.value().size() != 0 && data.sort_entries
                        && data.sort_entries.value().size() != 0;
    if (!common) { return false; }
    if (data.rasterization_mode == RasterizationMode::kComputeTiled) {
      return data.tiled_counts_buffer && data.tiled_offsets_buffer && data.tiled_total_buffer
             && data.tiled_overflow_buffer && data.tiled_radix_prefix_buffer && data.tiled_radix_group_buffer;
    }
    return data.rasterization_mode == RasterizationMode::kGraphicsPipeline;
  }

  [[nodiscard]] auto EnsureDescriptorHeapStorage(vulkan::Context &context, RenderData &data) -> bool
  {
    bool const needs_reallocation = !data.descriptor_heap_buffer || !data.sampler_heap_buffer
                                    || data.descriptor_heap_buffer.value().size() != data.descriptor_heap_size
                                    || data.sampler_heap_buffer.value().size() != data.sampler_heap_size;
    if (auto const idle = context.disp.deviceWaitIdle(); idle != VK_SUCCESS) {
      std::println("deviceWaitIdle failed before descriptor heap refresh: {}", static_cast<int>(idle));
      return false;
    }
    if (!needs_reallocation) { return true; }

    auto resource_heap =
      vulkan::CreateDescriptorHeapBuffer(*context.allocator, data.descriptor_heap_size, data.descriptor_heap_alignment);
    if (!resource_heap) {
      std::println("Failed to create shared resource heap buffer!");
      return false;
    }
    auto sampler_heap =
      vulkan::CreateDescriptorHeapBuffer(*context.allocator, data.sampler_heap_size, data.sampler_heap_alignment);
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
    return true;
  }

  [[nodiscard]] auto WriteGsDescriptorEntry(vulkan::Context const &context,
    vkexec::context const &vkexec,
    RenderData const &data,
    std::span<std::byte> heap_bytes,
    VkDeviceAddress device_address,
    vkexec::resource_ref const &resource,
    std::size_t descriptor_index) -> bool
  {
    if (descriptor_index >= kSharedResourceSlots) {
      std::println("GS resource descriptor index is outside the shared resource heap");
      return false;
    }
    if (device_address == 0 || resource.byte_size == 0) {
      std::println("GS resource descriptor has an invalid device address or empty range at slot {}", descriptor_index);
      return false;
    }
    if (resource.byte_size > context.device.physical_device.properties.limits.maxStorageBufferRange
        || device_address > std::numeric_limits<VkDeviceAddress>::max() - resource.byte_size) {
      std::println("GS resource descriptor range is invalid at slot {}", descriptor_index);
      return false;
    }
    auto const descriptor_offset = descriptor_index * data.descriptor_stride;
    if (descriptor_offset > heap_bytes.size() || data.buffer_descriptor_size > heap_bytes.size() - descriptor_offset) {
      std::println("GS resource descriptor is outside the mapped heap");
      return false;
    }
    auto const destination = heap_bytes.subspan(descriptor_offset, data.buffer_descriptor_size);
    auto const written =
      vkexec::write_storage_buffer_descriptor(vkexec, device_address, resource.byte_size, destination);
    if (!written) {
      std::println("Failed to write GS resource descriptor: {}", written.error().message());
      return false;
    }
    return true;
  }

  [[nodiscard]] auto WriteGsDescriptors(vulkan::Context const &context, vkexec::context const &vkexec, RenderData &data)
    -> bool
  {
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    auto const geometry_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianGeometry));
    auto const appearance_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianAppearance));
    auto const projected_buffer_size = data.projected_buffer.value().size();
    auto const unsorted_keys_buffer_size = data.unsorted_keys_buffer.value().size();
    auto const unsorted_values_buffer_size = data.unsorted_values_buffer.value().size();
    auto const sorted_keys_buffer_size = data.sorted_keys_buffer.value().size();
    auto const sorted_values_buffer_size = data.sorted_values_buffer.value().size();
    auto const sort_histogram_buffer_size = data.sort_histogram_buffer.value().size();
    auto const tile_ranges_buffer_size = data.tile_ranges_buffer.value().size();
    auto const sorted_indices_buffer_size = data.sorted_indices.value().byte_size();
    auto const sort_entries_buffer_size = data.sort_entries.value().byte_size();

    auto const resources = vkexec::make_resource_table(kGsHeapSchema,
      vkexec::buffer_resource(data.geometry_buffer.value().handle(), geometry_buffer_size),
      vkexec::buffer_resource(data.appearance_buffer.value().handle(), appearance_buffer_size),
      vkexec::buffer_resource(data.sorted_indices.value().vk_buffer(), sorted_indices_buffer_size),
      vkexec::buffer_resource(data.sort_entries.value().vk_buffer(), sort_entries_buffer_size),
      vkexec::buffer_resource(data.projected_buffer.value().handle(), projected_buffer_size),
      vkexec::buffer_resource(data.unsorted_keys_buffer.value().handle(), unsorted_keys_buffer_size),
      vkexec::buffer_resource(data.unsorted_values_buffer.value().handle(), unsorted_values_buffer_size),
      vkexec::buffer_resource(data.sorted_keys_buffer.value().handle(), sorted_keys_buffer_size),
      vkexec::buffer_resource(data.sorted_values_buffer.value().handle(), sorted_values_buffer_size),
      vkexec::buffer_resource(data.tile_ranges_buffer.value().handle(), tile_ranges_buffer_size),
      vkexec::storage_image_resource(data.color_image_view.value().handle(), VK_IMAGE_LAYOUT_GENERAL),
      vkexec::buffer_resource(data.sort_histogram_buffer.value().handle(), sort_histogram_buffer_size),
      vkexec::buffer_resource(
        data.projection_cameras_buffer.value().handle(), data.projection_cameras_buffer.value().size()));

    auto heap_bytes = data.descriptor_heap_buffer.value().mapped();
    std::array<VkDeviceAddress, GsHeapSchema::binding_count> const device_addresses{
      vulkan::DeviceAddressOrZero(data.geometry_buffer.value()),
      vulkan::DeviceAddressOrZero(data.appearance_buffer.value()),
      vulkan::DeviceAddressOrZero(data.sorted_indices.value().device()),
      vulkan::DeviceAddressOrZero(data.sort_entries.value().device()),
      vulkan::DeviceAddressOrZero(data.projected_buffer.value()),
      vulkan::DeviceAddressOrZero(data.unsorted_keys_buffer.value()),
      vulkan::DeviceAddressOrZero(data.unsorted_values_buffer.value()),
      vulkan::DeviceAddressOrZero(data.sorted_keys_buffer.value()),
      vulkan::DeviceAddressOrZero(data.sorted_values_buffer.value()),
      vulkan::DeviceAddressOrZero(data.tile_ranges_buffer.value()),
      0U,
      vulkan::DeviceAddressOrZero(data.sort_histogram_buffer.value()),
      vulkan::DeviceAddressOrZero(data.projection_cameras_buffer.value()),
    };
    // NOLINTEND(bugprone-unchecked-optional-access)
    auto const entries = resources.entries();
    if (entries.size() != kGsPhysicalIndices.size() || entries.size() != device_addresses.size()) {
      std::println("GS descriptor schema does not match its physical slot mapping");
      return false;
    }
    std::size_t entry_index{};
    for (auto const &entry : entries) {
      if (entry.resource.kind == vkexec::resource_kind::storage_image) {
        if (!data.color_image) {
          std::println("GS color image is missing");
          return false;
        }
        if (data.descriptor_stride < data.image_descriptor_size) {
          std::println("GS image descriptor exceeds descriptor stride");
          return false;
        }
        auto const descriptor_offset = static_cast<size_t>(HeapSlot::kColorTarget) * data.descriptor_stride;
        if (descriptor_offset > heap_bytes.size()
            || data.image_descriptor_size > heap_bytes.size() - descriptor_offset) {
          std::println("GS image descriptor is outside the mapped heap");
          return false;
        }
        VkImageViewCreateInfo const view_info{
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0U,
          .image = data.color_image->handle(),
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = data.color_format,
          .components = { .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY },
          .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0U,
            .levelCount = 1U,
            .baseArrayLayer = 0U,
            .layerCount = 1U },
        };
        auto const written = vkexec::write_storage_image_descriptor(vkexec,
          view_info,
          VK_IMAGE_LAYOUT_GENERAL,
          heap_bytes.subspan(descriptor_offset, data.image_descriptor_size));
        if (!written) {
          std::println("Failed to write GS color descriptor: {}", written.error().message());
          return false;
        }
        ++entry_index;
        continue;
      }
      if (!WriteGsDescriptorEntry(context,
            vkexec,
            data,
            heap_bytes,
            device_addresses.at(entry_index),
            entry.resource,
            kGsPhysicalIndices.at(entry_index))) {
        return false;
      }
      ++entry_index;
    }
    return true;
  }

  [[nodiscard]] auto FlushDescriptorHeaps(RenderData &data) -> bool
  {
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    if (auto const flushed = data.descriptor_heap_buffer.value().flush(); !flushed) {
      std::println("Failed to flush GS descriptor heap: {}", flushed.error().message());
      return false;
    }
    if (auto const flushed = data.sampler_heap_buffer.value().flush(); !flushed) {
      std::println("Failed to flush shared sampler heap: {}", flushed.error().message());
      return false;
    }
    // NOLINTEND(bugprone-unchecked-optional-access)
    return true;
  }

}// namespace

auto QueryDescriptorHeapLayout(vulkan::Context const &context, RenderData &data) -> bool
{
  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr || context.allocator == nullptr) { return false; }

  auto const layout = vkexec::query_descriptor_heap_layout(*vkexec);
  if (!layout) {
    std::println("query_descriptor_heap_layout failed: {}", layout.error().message());
    return false;
  }

  data.buffer_descriptor_size = layout->buffer_descriptor_size;
  data.image_descriptor_size = layout->image_descriptor_size;
  data.sampler_descriptor_size = layout->sampler_descriptor_size;
  data.descriptor_stride = layout->descriptor_stride;
  if (layout->resource_heap_alignment == 0 || layout->sampler_heap_alignment == 0
      || layout->descriptor_stride < layout->buffer_descriptor_size
      || layout->descriptor_stride < layout->image_descriptor_size || layout->sampler_descriptor_size == 0) {
    std::println("query_descriptor_heap_layout returned invalid descriptor sizes or alignments");
    return false;
  }
  data.descriptor_heap_alignment = layout->resource_heap_alignment;
  data.descriptor_heap_size = vkexec::descriptor_heap_byte_size(*layout, kSharedResourceSlots);
  auto const descriptor_region =
    static_cast<VkDeviceSize>(data.descriptor_stride) * static_cast<VkDeviceSize>(kSharedResourceSlots);
  data.reserved_range_offset = AlignUp(descriptor_region, layout->resource_heap_alignment);
  data.reserved_range_size = layout->min_resource_heap_reserved_range;
  data.sampler_heap_size = vkexec::sampler_heap_byte_size(*layout, kSharedSamplerSlots);
  data.sampler_heap_alignment = layout->sampler_heap_alignment;
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
  if (context.allocator == nullptr) { return false; }
  if (!QueryDescriptorHeapLayout(context, data)) { return false; }
  if (!HasGsBuffers(data)) {
    std::println("mandatory GS descriptor resources are missing");
    return false;
  }

  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr) { return false; }
  if (!EnsureDescriptorHeapStorage(context, data)) { return false; }
  if (!WriteGsDescriptors(context, *vkexec, data)) { return false; }
  return FlushDescriptorHeaps(data);
}

void BindDescriptorHeap(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
{
  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr || !data.descriptor_heap_buffer || !data.sampler_heap_buffer) { return; }

  VkDeviceAddress const heap_address = vulkan::DeviceAddressOrZero(data.descriptor_heap_buffer.value());
  auto const bound = vkexec::cmd_bind_resource_heap(*vkexec,
    command_buffer,
    heap_address,
    data.descriptor_heap_size,
    data.reserved_range_offset,
    data.reserved_range_size);
  if (!bound) { std::println("cmd_bind_resource_heap failed: {}", bound.error().message()); }

  VkDeviceAddress const sampler_heap_address = vulkan::DeviceAddressOrZero(data.sampler_heap_buffer.value());
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
