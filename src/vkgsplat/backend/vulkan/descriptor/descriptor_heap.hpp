#ifndef VKGSPLAT_BACKEND_VULKAN_DESCRIPTOR_DESCRIPTOR_HEAP_HPP
#define VKGSPLAT_BACKEND_VULKAN_DESCRIPTOR_DESCRIPTOR_HEAP_HPP

#include <cstddef>
#include <cstdint>
#include <span>

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

enum class HeapSlot : size_t {
  kGeometry = 0,
  kAppearance = 1,
  kSortedIndices = 2,
  kSortEntries = 3,
  kProjected = 4,
  kUnsortedKeys = 5,
  kUnsortedValues = 6,
  kSortedKeys = 7,
  kSortedValues = 8,
  kTileRanges = 9,
  kColorTarget = 10,
  kSortHistogram = 11,
};

inline constexpr size_t kHeapDescriptorCount = 12;

[[nodiscard]] constexpr auto AlignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept -> VkDeviceSize
{ return (value + alignment - 1) / alignment * alignment; }

[[nodiscard]] auto WriteStorageBufferDescriptor(Init &init,
  VkDeviceAddress buffer_address,
  VkDeviceSize buffer_size,
  std::span<std::byte> destination) -> bool;

[[nodiscard]] auto WriteStorageImageDescriptor(Init &init,
  VkImageViewCreateInfo const &view_info,
  VkImageLayout layout,
  std::span<std::byte> destination) -> bool;

[[nodiscard]] auto QueryDescriptorHeapLayout(Init const &init, RenderData &data) -> bool;

void DestroyDescriptorHeap(Init &init, RenderData &data);

[[nodiscard]] auto RefreshDescriptorHeap(Init &init, RenderData &data) -> bool;

void BindDescriptorHeap(Init const &init, RenderData const &data, VkCommandBuffer command_buffer);

[[nodiscard]] auto HeapSlotByteOffset(RenderData const &data, HeapSlot slot) -> uint32_t;

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_DESCRIPTOR_DESCRIPTOR_HEAP_HPP
