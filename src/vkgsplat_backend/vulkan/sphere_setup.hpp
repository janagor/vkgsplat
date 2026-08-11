#ifndef VKGSPLAT_BACKEND_VULKAN_SPHERE_SETUP_HPP
#define VKGSPLAT_BACKEND_VULKAN_SPHERE_SETUP_HPP

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

[[nodiscard]] auto InitSphereSetup(vulkan::Context &context, RenderData &data) -> bool;

void DestroySphereSetup(vulkan::Context &context, RenderData &data);

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_SPHERE_SETUP_HPP
