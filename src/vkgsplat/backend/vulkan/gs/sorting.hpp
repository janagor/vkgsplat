#ifndef VKGSPLAT_BACKEND_VULKAN_GS_SORTING_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_SORTING_HPP

#include "app_state.hpp"

namespace vkgsplat::gs {

[[nodiscard]] auto InitSorting(Init &init, RenderData &data) -> bool;

void DestroySorting(Init &init, RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_SORTING_HPP
