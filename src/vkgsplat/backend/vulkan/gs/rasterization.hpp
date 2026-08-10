#ifndef VKGSPLAT_BACKEND_VULKAN_GS_RASTERIZATION_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_RASTERIZATION_HPP

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

[[nodiscard]] auto InitRasterization(vulkan::Context &context, RenderData &data) -> bool;

[[nodiscard]] auto RecreateRasterizationColorTarget(vulkan::Context &context, RenderData &data) -> bool;

void DispatchRasterization(vulkan::Context const &context,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index);

void DestroyRasterization(vulkan::Context &context, RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_RASTERIZATION_HPP
