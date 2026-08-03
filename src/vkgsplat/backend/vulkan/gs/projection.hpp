#pragma once

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

[[nodiscard]] auto InitProjection(Init &init, RenderData &data) -> bool;

void DispatchProjection(Init const &init,
  RenderData const &data,
  ProjectPushConstants const &push_constants,
  VkCommandBuffer command_buffer);

void DestroyProjection(Init &init, RenderData &data);

}// namespace vkgsplat::gs
