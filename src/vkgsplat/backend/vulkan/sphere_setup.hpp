#ifndef VKGSPLAT_BACKEND_VULKAN_SPHERE_SETUP_HPP
#define VKGSPLAT_BACKEND_VULKAN_SPHERE_SETUP_HPP

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

[[nodiscard]] auto InitSphereSetup(Init &init, RenderData &data) -> bool;

void DestroySphereSetup(Init &init, RenderData &data);

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_SPHERE_SETUP_HPP
