#ifndef VKGSPLAT_BACKEND_VULKAN_MESH_GPU_HPP
#define VKGSPLAT_BACKEND_VULKAN_MESH_GPU_HPP

#include "app_state.hpp"
#include "io/ply/load_splats.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

void DestroySphereBuffers(Init &init, RenderData &data);

[[nodiscard]] auto CreateSphereBuffers(Init &init, RenderData &data, SplatCpuData const &cpu_data) -> bool;

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_MESH_GPU_HPP
