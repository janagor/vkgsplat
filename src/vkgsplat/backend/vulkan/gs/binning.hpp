#pragma once

#include "app_state.hpp"

namespace vkgsplat::gs {

[[nodiscard]] auto init_binning(Init &init, RenderData &data) -> bool;

void destroy_binning(Init &init, RenderData &data);

}// namespace vkgsplat::gs
