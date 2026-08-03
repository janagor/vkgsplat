#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

void DestroyDepthBuffer(Init &init, RenderData &data);

[[nodiscard]] auto CreateDepthBuffer(Init &init, RenderData &data) -> bool;

}// namespace vkgsplat
