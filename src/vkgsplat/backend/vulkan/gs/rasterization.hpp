#pragma once

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

[[nodiscard]] auto InitRasterization(Init &init, RenderData &data) -> bool;

[[nodiscard]] auto RecreateRasterizationColorTarget(Init &init, RenderData &data) -> bool;

void DispatchRasterization(Init const &init,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index);

void DestroyRasterization(Init &init, RenderData &data);

}// namespace vkgsplat::gs
