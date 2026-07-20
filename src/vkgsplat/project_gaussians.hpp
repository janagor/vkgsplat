#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/camera.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto init_project_gaussians(Init &init, RenderData &data) -> bool;

void dispatch_project_gaussians(Init const &init,
  RenderData const &data,
  ProjectPushConstants const &push_constants,
  VkCommandBuffer command_buffer);

void destroy_project_gaussians(Init &init, RenderData &data);

}// namespace vkgsplat
