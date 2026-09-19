#ifndef VKGSPLAT_BACKEND_VULKAN_DESCRIPTOR_DESCRIPTOR_HEAP_HPP
#define VKGSPLAT_BACKEND_VULKAN_DESCRIPTOR_DESCRIPTOR_HEAP_HPP

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

enum class HeapSlot : u8 {
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
inline constexpr uint32_t kImguiImageBase = 12;
inline constexpr uint32_t kImguiImageSlots = 8;
inline constexpr size_t kSharedResourceSlots = kImguiImageBase + kImguiImageSlots;
inline constexpr size_t kSharedSamplerSlots = 2;

[[nodiscard]] constexpr auto AlignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept -> VkDeviceSize
{ return (value + alignment - 1) / alignment * alignment; }

[[nodiscard]] auto QueryDescriptorHeapLayout(vulkan::Context const &context, RenderData &data) -> bool;

void DestroyDescriptorHeap(vulkan::Context &context, RenderData &data);

[[nodiscard]] auto RefreshDescriptorHeap(vulkan::Context &context, RenderData &data) -> bool;

void BindDescriptorHeap(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer);

[[nodiscard]] auto HeapSlotByteOffset(RenderData const &data, HeapSlot slot) -> uint32_t;

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_DESCRIPTOR_DESCRIPTOR_HEAP_HPP
