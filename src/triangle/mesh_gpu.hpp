#pragma once

#include "app_state.hpp"

namespace vkgsplat {

void destroy_mesh_buffers(Init &init, RenderData &data);

[[nodiscard]] auto upload_mesh_buffers(Init &init, RenderData &data) -> bool;

[[nodiscard]] auto refresh_mesh_descriptor_heap(Init &init, RenderData &data) -> bool;

}// namespace vkgsplat
