#pragma once

#include "app_state.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto init_triangle_sort(Init &init, RenderData &data) -> bool;

void dispatch_triangle_sort(Init const &init, RenderData const &data, VkCommandBuffer command_buffer);

void destroy_triangle_sort(Init &init, RenderData &data);

}// namespace vkgsplat
