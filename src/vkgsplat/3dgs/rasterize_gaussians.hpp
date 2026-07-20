#pragma once

#include "app_state.hpp"
#include "3dgs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

[[nodiscard]] auto init_rasterize_gaussians(Init &init, RenderData &data) -> bool;

[[nodiscard]] auto recreate_rasterize_color_target(Init &init, RenderData &data) -> bool;

void dispatch_rasterize_gaussians(Init const &init,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index);

void destroy_rasterize_gaussians(Init &init, RenderData &data);

}// namespace vkgsplat::gs
