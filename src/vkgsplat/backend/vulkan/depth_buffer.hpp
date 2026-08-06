#ifndef VKGSPLAT_BACKEND_VULKAN_DEPTH_BUFFER_HPP
#define VKGSPLAT_BACKEND_VULKAN_DEPTH_BUFFER_HPP

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

void DestroyDepthBuffer(Init &init, RenderData &data);

[[nodiscard]] auto CreateDepthBuffer(Init &init, RenderData &data) -> bool;

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_DEPTH_BUFFER_HPP
