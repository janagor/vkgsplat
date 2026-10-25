#ifndef VKGSPLAT_BACKEND_VULKAN_GS_SORTING_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_SORTING_HPP

#include "app_state.hpp"

namespace vkgsplat::gs {

[[nodiscard]] auto InitSorting(vulkan::Context &context, RenderData &data) -> bool;

void DestroySorting(vulkan::Context &context, RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_SORTING_HPP
