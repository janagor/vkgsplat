#include "mesh_gpu.hpp"

#include <print>
#include <span>

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "io/ply/load_splats.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void destroy_sphere_buffers(Init &init, RenderData &data)
{
  init.gpu_allocator.destroy_buffer(data.geometry_buffer);
  init.gpu_allocator.destroy_buffer(data.appearance_buffer);
  init.gpu_allocator.destroy_buffer(data.projected_buffer);
  data.geometry_buffer = {};
  data.appearance_buffer = {};
  data.projected_buffer = {};
}

auto create_sphere_buffers(Init &init, RenderData &data, SplatCpuData const &cpu_data) -> bool
{
  destroy_sphere_buffers(init, data);

  if (cpu_data.geometries.size() != cpu_data.appearances.size() || cpu_data.geometries.empty()) {
    std::println("Invalid splat CPU data!");
    return false;
  }
  data.splat_count = static_cast<u32>(cpu_data.geometries.size());

  auto const geometry_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianGeometry));
  auto const appearance_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianAppearance));
  auto const projected_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianProjected));

  auto geometry_buffer = init.gpu_allocator.create_storage_buffer(geometry_buffer_size);
  auto appearance_buffer = init.gpu_allocator.create_storage_buffer(appearance_buffer_size);
  auto projected_buffer = init.gpu_allocator.create_device_storage_buffer(projected_buffer_size);
  if (!geometry_buffer || !appearance_buffer || !projected_buffer) {
    std::println("Failed to create gaussian buffers!");
    return false;
  }

  data.geometry_buffer = *geometry_buffer;
  data.appearance_buffer = *appearance_buffer;
  data.projected_buffer = *projected_buffer;

  if (!init.gpu_allocator.write_buffer(*geometry_buffer, std::span{ cpu_data.geometries })) {
    std::println("Failed to upload splat geometry!");
    return false;
  }

  if (!init.gpu_allocator.write_buffer(*appearance_buffer, std::span{ cpu_data.appearances })) {
    std::println("Failed to upload splat appearance!");
    return false;
  }

  return true;
}

}// namespace vkgsplat
