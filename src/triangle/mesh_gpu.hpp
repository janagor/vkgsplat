#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

void destroy_sphere_buffers(Init &init, RenderData &data);

[[nodiscard]] auto create_sphere_buffers(Init &init, RenderData &data) -> bool;

}// namespace vkgsplat
