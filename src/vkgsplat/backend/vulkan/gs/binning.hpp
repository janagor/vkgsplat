#ifndef VKGSPLAT_BACKEND_VULKAN_GS_BINNING_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_BINNING_HPP

#include "app_state.hpp"

namespace vkgsplat::gs {

[[nodiscard]] auto InitBinning(Init &init, RenderData &data) -> bool;

void DestroyBinning(Init &init, RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_BINNING_HPP
