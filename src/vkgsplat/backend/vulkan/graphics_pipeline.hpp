#ifndef VKGSPLAT_BACKEND_VULKAN_GRAPHICS_PIPELINE_HPP
#define VKGSPLAT_BACKEND_VULKAN_GRAPHICS_PIPELINE_HPP

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

[[nodiscard]] auto CreateGraphicsPipeline(vulkan::Context &context, RenderData &data) -> int;

void DestroyGraphicsPipeline(vulkan::Context &context, RenderData &data);

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GRAPHICS_PIPELINE_HPP
