#include "vulkan/descriptor/descriptor_heap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <utility>
#include <vector>

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vkexec/context.hpp>
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

}// namespace

auto WriteStorageBufferDescriptor(vulkan::Context const &context,
  VkDeviceAddress buffer_address,
  VkDeviceSize buffer_size,
  std::span<std::byte> destination) -> bool
{
  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr) { return false; }

  auto const wrote = vkexec::write_storage_buffer_descriptor(*vkexec, buffer_address, buffer_size, destination);
  if (!wrote) {
    std::println("write_storage_buffer_descriptor failed: {}", wrote.error().message());
    return false;
  }
  return true;
}

auto WriteStorageImageDescriptor(vulkan::Context const &context,
  VkImageViewCreateInfo const &view_info,
  VkImageLayout layout,
  std::span<std::byte> destination) -> bool
{
  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr) { return false; }

  auto const wrote = vkexec::write_storage_image_descriptor(*vkexec, view_info, layout, destination);
  if (!wrote) {
    std::println("write_storage_image_descriptor failed: {}", wrote.error().message());
    return false;
  }
  return true;
}

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
  data.descriptor_stride = layout->descriptor_stride;
  data.descriptor_heap_size = vkexec::descriptor_heap_byte_size(*layout, kHeapDescriptorCount);
  auto const descriptor_region =
    static_cast<VkDeviceSize>(data.descriptor_stride) * static_cast<VkDeviceSize>(kHeapDescriptorCount);
  data.reserved_range_offset = AlignUp(descriptor_region, layout->resource_heap_alignment);
  data.reserved_range_size = layout->min_resource_heap_reserved_range;
  return data.descriptor_stride > 0;
}

void DestroyDescriptorHeap(vulkan::Context &context, RenderData &data)
{
  context.gpu_allocator.destroy_buffer(data.descriptor_heap_buffer);
  data.descriptor_heap_buffer = {};
}

auto RefreshDescriptorHeap(vulkan::Context &context, RenderData &data) -> bool
{
  if (!QueryDescriptorHeapLayout(context, data)) { return false; }

  DestroyDescriptorHeap(context, data);

  if (data.geometry_buffer.empty() || data.appearance_buffer.empty() || data.projected_buffer.empty()
      || data.unsorted_keys_buffer.empty() || data.unsorted_values_buffer.empty() || data.sorted_keys_buffer.empty()
      || data.sorted_values_buffer.empty() || data.sort_histogram_buffer.empty() || data.tile_ranges_buffer.empty()
      || data.sorted_indices.buffer().empty() || data.sort_entries.buffer().empty()) {
    return true;
  }

  auto descriptor_heap_buffer = context.gpu_allocator.create_heap_buffer(data.descriptor_heap_size);
  if (!descriptor_heap_buffer) {
    std::println("Failed to create descriptor heap buffer!");
    return false;
  }

  auto const descriptor_size = data.descriptor_stride;
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
  auto const sorted_indices_buffer_size = data.sorted_indices.byte_size();
  auto const sort_entries_buffer_size = data.sort_entries.byte_size();

  std::vector<std::byte> descriptor_data(data.descriptor_stride * kHeapDescriptorCount);
  std::array<VkDeviceAddressRangeEXT, kHeapDescriptorCount> address_ranges{};
  address_ranges.at(static_cast<size_t>(HeapSlot::kGeometry)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.geometry_buffer),
    .size = geometry_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kAppearance)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.appearance_buffer),
    .size = appearance_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortedIndices)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sorted_indices.buffer()),
    .size = sorted_indices_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortEntries)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sort_entries.buffer()),
    .size = sort_entries_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kProjected)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.projected_buffer),
    .size = projected_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kUnsortedKeys)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.unsorted_keys_buffer),
    .size = unsorted_keys_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kUnsortedValues)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.unsorted_values_buffer),
    .size = unsorted_values_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortedKeys)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sorted_keys_buffer),
    .size = sorted_keys_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortedValues)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sorted_values_buffer),
    .size = sorted_values_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kTileRanges)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.tile_ranges_buffer),
    .size = tile_ranges_buffer_size,
  };
  address_ranges.at(static_cast<size_t>(HeapSlot::kSortHistogram)) = {
    .address = context.gpu_allocator.get_buffer_device_address(data.sort_histogram_buffer),
    .size = sort_histogram_buffer_size,
  };

  for (size_t i = 0; i < kHeapDescriptorCount; ++i) {
    auto const slot = static_cast<HeapSlot>(i);
    auto destination = std::span{ descriptor_data }.subspan(i * data.descriptor_stride, descriptor_size);

    if (slot == HeapSlot::kColorTarget) {
      // HW path: float color attachment + blit; not a storage image.
      continue;
    }

    if (!WriteStorageBufferDescriptor(context,
          address_ranges.at(i).address,
          address_ranges.at(i).size,
          destination.first(data.buffer_descriptor_size))) {
      std::println("Failed to write descriptor heap slot {}!", i);
      return false;
    }
  }

  if (!context.gpu_allocator.write_buffer<std::byte>(*descriptor_heap_buffer, std::span{ descriptor_data })) {
    std::println("Failed to upload descriptor heap!");
    return false;
  }

  data.descriptor_heap_buffer = std::move(*descriptor_heap_buffer);
  return true;
}

void BindDescriptorHeap(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer)
{
  auto *vkexec = RequireVkexec(context);
  if (vkexec == nullptr) { return; }

  VkDeviceAddress const heap_address = context.gpu_allocator.get_buffer_device_address(data.descriptor_heap_buffer);
  auto const bound = vkexec::cmd_bind_resource_heap(*vkexec,
    command_buffer,
    heap_address,
    data.descriptor_heap_size,
    data.reserved_range_offset,
    data.reserved_range_size);
  if (!bound) { std::println("cmd_bind_resource_heap failed: {}", bound.error().message()); }
}

auto HeapSlotByteOffset(RenderData const &data, HeapSlot slot) -> uint32_t
{ return static_cast<uint32_t>(static_cast<size_t>(slot) * data.descriptor_stride); }

}// namespace vkgsplat
