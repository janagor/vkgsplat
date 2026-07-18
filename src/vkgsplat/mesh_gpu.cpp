#include "mesh_gpu.hpp"

#include <array>
#include <functional>
#include <optional>
#include <print>
#include <span>
#include <vector>

#include "io/ply/load_splats.hpp"
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

auto create_sphere_buffers(Init &init,
  RenderData &data,
  u32 splat_count,
  std::optional<std::reference_wrapper<SplatCpuData const>> cpu_data) -> bool
{
  destroy_sphere_buffers(init, data);

  if (cpu_data) {
    auto const &splats = cpu_data->get();
    if (splats.positions.size() != splats.gray_colors.size() || splats.positions.empty()) {
      std::println("Invalid splat CPU data!");
      return false;
    }
    data.splat_count = static_cast<u32>(splats.positions.size());
  } else {
    if (splat_count == 0) {
      std::println("Splat count must be greater than zero!");
      return false;
    }
    data.splat_count = splat_count;
  }

  auto const position_buffer_size =
    static_cast<VkDeviceSize>(data.splat_count * sizeof(std::array<f32, 3>));
  auto const color_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(f32));

  auto position_buffer = init.gpu_allocator.create_storage_buffer(position_buffer_size);
  auto color_buffer = init.gpu_allocator.create_storage_buffer(color_buffer_size);
  if (!position_buffer || !color_buffer) {
    std::println("Failed to create sphere buffers!");
    return false;
  }

  data.position_buffer = *position_buffer;
  data.color_buffer = *color_buffer;

  if (cpu_data) {
    auto const &splats = cpu_data->get();
    if (!init.gpu_allocator.write_buffer(*position_buffer, std::span{ splats.positions })) {
      std::println("Failed to upload splat positions!");
      return false;
    }

    if (!init.gpu_allocator.write_buffer(*color_buffer, std::span{ splats.gray_colors })) {
      std::println("Failed to upload splat colors!");
      return false;
    }
    return true;
  }

  std::vector<f32> const zero_colors(data.splat_count, 0.0F);
  if (!init.gpu_allocator.write_buffer(*color_buffer, std::span<const f32>{ zero_colors })) {
    std::println("Failed to zero-initialize color buffer!");
    return false;
  }

  return true;
}

}// namespace vkgsplat
