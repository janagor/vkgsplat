#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto InitSphereSetup(Init &init, RenderData &data) -> bool;

void DispatchSphereSetup(Init &init, RenderData const &data, VkCommandBuffer command_buffer);

void DestroySphereSetup(Init &init, RenderData &data);

}// namespace vkgsplat
