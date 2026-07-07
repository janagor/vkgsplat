#include "mesh_gpu.hpp"

#include <algorithm>
#include <array>
#include <print>
#include <span>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include "types.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void destroy_mesh_buffers(Init &init, RenderData &data)
{
  init.gpu_allocator.destroy_buffer(data.position_buffer);
  init.gpu_allocator.destroy_buffer(data.color_buffer);
  data.position_buffer = {};
  data.color_buffer = {};
}

auto upload_mesh_buffers(Init &init, RenderData &data) -> bool
{
  destroy_mesh_buffers(init, data);

  if (data.mesh.vertex_count() == 0) { return true; }

  if (data.mesh.positions.size() != data.mesh.colors.size()) {
    std::println("Mesh positions/colors size mismatch!");
    return false;
  }

  auto const vertex_count = data.mesh.vertex_count();
  if (data.mesh_buffer_vertex_capacity < vertex_count) {
    data.mesh_buffer_vertex_capacity =
      std::max({ vertex_count, data.mesh_buffer_vertex_capacity * 2, k_mesh_buffer_min_vertex_capacity });
  }

  auto const position_buffer_size = static_cast<VkDeviceSize>(
    data.mesh_buffer_vertex_capacity * sizeof(data.mesh.positions.front()));
  auto const color_buffer_size =
    static_cast<VkDeviceSize>(data.mesh_buffer_vertex_capacity * sizeof(data.mesh.colors.front()));

  auto position_buffer = init.gpu_allocator.create_storage_buffer(position_buffer_size);
  auto color_buffer = init.gpu_allocator.create_storage_buffer(color_buffer_size);
  if (!position_buffer || !color_buffer) {
    std::println("Failed to create mesh vertex buffers!");
    return false;
  }

  if (!init.gpu_allocator.write_buffer(
        *position_buffer, std::span<const std::array<f32, 2>>{ data.mesh.positions })) {
    std::println("Failed to upload position buffer!");
    return false;
  }

  if (!init.gpu_allocator.write_buffer(*color_buffer, std::span<const std::array<f32, 3>>{ data.mesh.colors })) {
    std::println("Failed to upload color buffer!");
    return false;
  }

  data.position_buffer = *position_buffer;
  data.color_buffer = *color_buffer;
  return true;
}

}// namespace vkgsplat
