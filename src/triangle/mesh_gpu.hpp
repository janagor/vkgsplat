#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

void destroy_mesh_buffers(Init &init, RenderData &data);

[[nodiscard]] auto upload_mesh_buffers(Init &init, RenderData &data) -> bool;

}// namespace vkgsplat
