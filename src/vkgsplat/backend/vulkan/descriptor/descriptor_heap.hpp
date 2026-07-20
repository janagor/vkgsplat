#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

enum class HeapSlot : size_t
{
  Geometry = 0,
  Appearance = 1,
  SortedIndices = 2,
  SortEntries = 3,
};

inline constexpr size_t k_heap_descriptor_count = 4;

[[nodiscard]] auto align_up(VkDeviceSize value, VkDeviceSize alignment) -> VkDeviceSize;

[[nodiscard]] auto write_storage_buffer_descriptor(Init &init,
  VkDeviceAddress buffer_address,
  VkDeviceSize buffer_size,
  std::span<std::byte> destination) -> bool;

[[nodiscard]] auto query_descriptor_heap_layout(Init const &init, RenderData &data) -> bool;

void destroy_descriptor_heap(Init &init, RenderData &data);

[[nodiscard]] auto refresh_descriptor_heap(Init &init, RenderData &data) -> bool;

void bind_descriptor_heap(Init const &init, RenderData const &data, VkCommandBuffer command_buffer);

[[nodiscard]] auto heap_slot_byte_offset(RenderData const &data, HeapSlot slot) -> uint32_t;

}// namespace vkgsplat
