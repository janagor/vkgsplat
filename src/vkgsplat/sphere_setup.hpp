#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto init_sphere_setup(Init &init, RenderData &data) -> bool;

void dispatch_sphere_setup(Init &init, RenderData const &data, VkCommandBuffer command_buffer);

void destroy_sphere_setup(Init &init, RenderData &data);

}// namespace vkgsplat
