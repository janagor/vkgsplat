#pragma once

#include <optional>

#include "app_state.hpp"
#include "io/ply/load_splats.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

void destroy_sphere_buffers(Init &init, RenderData &data);

[[nodiscard]] auto create_sphere_buffers(Init &init,
  RenderData &data,
  u32 splat_count,
  std::optional<std::reference_wrapper<SplatCpuData const>> cpu_data = std::nullopt) -> bool;

}// namespace vkgsplat
