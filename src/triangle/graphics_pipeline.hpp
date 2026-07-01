#pragma once

#include "app_state.hpp"

namespace vkgsplat {

[[nodiscard]] auto create_graphics_pipeline(Init &init, RenderData &data) -> int;

}// namespace vkgsplat
