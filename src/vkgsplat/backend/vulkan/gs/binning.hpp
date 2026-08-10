#ifndef VKGSPLAT_BACKEND_VULKAN_GS_BINNING_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_BINNING_HPP

#include "app_state.hpp"

namespace vkgsplat::gs {

[[nodiscard]] auto InitBinning(vulkan::Context &context, RenderData &data) -> bool;

void DestroyBinning(vulkan::Context &context, RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_BINNING_HPP
