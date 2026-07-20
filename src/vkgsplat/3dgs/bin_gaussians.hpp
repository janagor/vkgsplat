#pragma once

#include "app_state.hpp"
#include "3dgs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto init_bin_gaussians(Init &init, RenderData &data) -> bool;

void dispatch_bin_gaussians(Init const &init,
  RenderData const &data,
  BinPushConstants const &push_constants,
  VkCommandBuffer command_buffer);

void destroy_bin_gaussians(Init &init, RenderData &data);

}// namespace vkgsplat
