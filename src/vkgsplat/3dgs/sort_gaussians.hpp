#pragma once

#include "app_state.hpp"
#include "3dgs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto init_sort_gaussians(Init &init, RenderData &data) -> bool;

void dispatch_sort_gaussians(Init const &init,
  RenderData const &data,
  SortPushConstants const &push_constants,
  VkCommandBuffer command_buffer);

void destroy_sort_gaussians(Init &init, RenderData &data);

}// namespace vkgsplat
