#pragma once

#include "app_state.hpp"
#include "io/ply/load_splats.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

void destroy_sphere_buffers(Init &init, RenderData &data);

[[nodiscard]] auto create_sphere_buffers(Init &init, RenderData &data, SplatCpuData const &cpu_data) -> bool;

}// namespace vkgsplat
