#pragma once

#include "app_state.hpp"

namespace vkgsplat::gs {

[[nodiscard]] auto InitBinning(Init &init, RenderData &data) -> bool;

void DestroyBinning(Init &init, RenderData &data);

}// namespace vkgsplat::gs
