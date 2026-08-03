#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

[[nodiscard]] auto CreateGraphicsPipeline(Init &init, RenderData &data) -> int;

void DestroyGraphicsPipeline(Init &init, RenderData &data);

}// namespace vkgsplat
