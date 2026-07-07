#include "mesh_gpu.hpp"

#include <array>
#include <print>
#include <span>
#include <vector>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include "types.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void destroy_sphere_buffers(Init &init, RenderData &data)
{
  init.gpu_allocator.destroy_buffer(data.position_buffer);
  init.gpu_allocator.destroy_buffer(data.color_buffer);
  data.position_buffer = {};
  data.color_buffer = {};
}

auto create_sphere_buffers(Init &init, RenderData &data) -> bool
{
  destroy_sphere_buffers(init, data);

  auto const position_buffer_size = static_cast<VkDeviceSize>(k_sphere_count * sizeof(std::array<f32, 3>));
  auto const color_buffer_size = static_cast<VkDeviceSize>(k_sphere_count * sizeof(f32));

  auto position_buffer = init.gpu_allocator.create_storage_buffer(position_buffer_size);
  auto color_buffer = init.gpu_allocator.create_storage_buffer(color_buffer_size);
  if (!position_buffer || !color_buffer) {
    std::println("Failed to create sphere buffers!");
    return false;
  }

  data.position_buffer = *position_buffer;
  data.color_buffer = *color_buffer;

  std::vector<f32> const zero_colors(k_sphere_count, 0.0F);
  if (!init.gpu_allocator.write_buffer(*color_buffer, std::span<const f32>{ zero_colors })) {
    std::println("Failed to zero-initialize color buffer!");
    return false;
  }

  return true;
}

}// namespace vkgsplat
