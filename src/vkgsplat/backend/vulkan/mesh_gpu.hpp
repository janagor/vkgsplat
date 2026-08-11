#ifndef VKGSPLAT_BACKEND_VULKAN_MESH_GPU_HPP
#define VKGSPLAT_BACKEND_VULKAN_MESH_GPU_HPP

#include "app_state.hpp"
#include <vkgsplat_io/splat_cpu.hpp>
#include "vulkan_context.hpp"

namespace vkgsplat {

void DestroySphereBuffers(vulkan::Context &context, RenderData &data);

[[nodiscard]] auto CreateSphereBuffers(vulkan::Context &context, RenderData &data, SplatCpuData const &cpu_data) -> bool;

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_MESH_GPU_HPP
