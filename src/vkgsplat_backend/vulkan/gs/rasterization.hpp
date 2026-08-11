#ifndef VKGSPLAT_BACKEND_VULKAN_GS_RASTERIZATION_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_RASTERIZATION_HPP

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

[[nodiscard]] auto InitRasterization(vulkan::Context &context, RenderData &data) -> bool;

[[nodiscard]] auto RecreateRasterizationColorTarget(vulkan::Context &context, RenderData &data) -> bool;

// Mono path: clear color target, draw full extent, blit to swapchain.
void DispatchRasterization(vulkan::Context const &context,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index);

// Quilt path helpers (atlas RT). Call Prepare once, DrawTile per cell, then Blit.
void PrepareQuiltPresent(vulkan::Context const &context,
  RenderData const &data,
  VkCommandBuffer command_buffer,
  size_t image_index);

void DrawQuiltTile(vulkan::Context const &context,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  VkRect2D tile,
  bool clear_attachment);

void BlitQuiltToSwapchain(vulkan::Context const &context,
  RenderData const &data,
  VkCommandBuffer command_buffer,
  size_t image_index);

void DestroyRasterization(vulkan::Context &context, RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_RASTERIZATION_HPP
