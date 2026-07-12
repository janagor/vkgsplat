#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

[[nodiscard]] auto create_graphics_pipeline(Init &init, RenderData &data) -> int;

void destroy_graphics_pipeline(Init &init, RenderData &data);

}// namespace vkgsplat
