#pragma once

#include <cstddef>
#include <span>

#include "app_state.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto align_up(VkDeviceSize value, VkDeviceSize alignment) -> VkDeviceSize;

[[nodiscard]] auto write_storage_buffer_descriptor(Init &init,
  VkDeviceAddress buffer_address,
  VkDeviceSize buffer_size,
  std::span<std::byte> destination) -> bool;

void bind_mesh_descriptor_heap(Init const &init, RenderData const &data, VkCommandBuffer command_buffer);

}// namespace vkgsplat
