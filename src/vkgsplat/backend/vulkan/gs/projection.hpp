#pragma once

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

[[nodiscard]] auto init_projection(Init &init, RenderData &data) -> bool;

void dispatch_projection(Init const &init,
  RenderData const &data,
  ProjectPushConstants const &push_constants,
  VkCommandBuffer command_buffer);

void destroy_projection(Init &init, RenderData &data);

}// namespace vkgsplat::gs
