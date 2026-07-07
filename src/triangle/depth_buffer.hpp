#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

void destroy_depth_buffer(Init &init, RenderData &data);

[[nodiscard]] auto create_depth_buffer(Init &init, RenderData &data) -> bool;

}// namespace vkgsplat
