#pragma once

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

[[nodiscard]] auto init_binning(Init &init, RenderData &data) -> bool;

void dispatch_binning(Init const &init,
  RenderData const &data,
  BinPushConstants const &push_constants,
  VkCommandBuffer command_buffer);

void destroy_binning(Init &init, RenderData &data);

}// namespace vkgsplat::gs
