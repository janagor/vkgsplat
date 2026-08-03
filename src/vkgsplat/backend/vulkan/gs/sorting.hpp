#pragma once

#include "app_state.hpp"

namespace vkgsplat::gs {

[[nodiscard]] auto InitSorting(Init &init, RenderData &data) -> bool;

void DestroySorting(Init &init, RenderData &data);

}// namespace vkgsplat::gs
