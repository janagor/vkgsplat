#pragma once

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

[[nodiscard]] auto init_rasterization(Init &init, RenderData &data) -> bool;

[[nodiscard]] auto recreate_rasterization_color_target(Init &init, RenderData &data) -> bool;

void dispatch_rasterization(Init const &init,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index);

void destroy_rasterization(Init &init, RenderData &data);

}// namespace vkgsplat::gs
