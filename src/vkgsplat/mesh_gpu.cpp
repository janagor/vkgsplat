#include "mesh_gpu.hpp"

#include <functional>
#include <optional>
#include <print>
#include <span>
#include <vector>

#include "app_state.hpp"
#include "3dgs/gaussian_splat.hpp"
#include "io/ply/load_splats.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

using namespace gs;

void destroy_sphere_buffers(Init &init, RenderData &data)
{
  init.gpu_allocator.destroy_buffer(data.geometry_buffer);
  init.gpu_allocator.destroy_buffer(data.appearance_buffer);
  init.gpu_allocator.destroy_buffer(data.projected_buffer);
  data.geometry_buffer = {};
  data.appearance_buffer = {};
  data.projected_buffer = {};
}

auto create_sphere_buffers(Init &init,
  RenderData &data,
  u32 splat_count,
  std::optional<std::reference_wrapper<SplatCpuData const>> cpu_data) -> bool
{
  destroy_sphere_buffers(init, data);

  if (cpu_data) {
    auto const &splats = cpu_data->get();
    if (splats.geometries.size() != splats.appearances.size() || splats.geometries.empty()) {
      std::println("Invalid splat CPU data!");
      return false;
    }
    data.splat_count = static_cast<u32>(splats.geometries.size());
  } else {
    if (splat_count == 0) {
      std::println("Splat count must be greater than zero!");
      return false;
    }
    data.splat_count = splat_count;
  }

  auto const geometry_buffer_size =
    static_cast<VkDeviceSize>(data.splat_count * sizeof(GaussianGeometry));
  auto const appearance_buffer_size =
    static_cast<VkDeviceSize>(data.splat_count * sizeof(GaussianAppearance));
  auto const projected_buffer_size =
    static_cast<VkDeviceSize>(data.splat_count * sizeof(GaussianProjected));

  auto geometry_buffer = init.gpu_allocator.create_storage_buffer(geometry_buffer_size);
  auto appearance_buffer = init.gpu_allocator.create_storage_buffer(appearance_buffer_size);
  auto projected_buffer = init.gpu_allocator.create_storage_buffer(projected_buffer_size);
  if (!geometry_buffer || !appearance_buffer || !projected_buffer) {
    std::println("Failed to create gaussian buffers!");
    return false;
  }

  data.geometry_buffer = *geometry_buffer;
  data.appearance_buffer = *appearance_buffer;
  data.projected_buffer = *projected_buffer;

  std::vector<GaussianProjected> const zero_projected(data.splat_count);
  if (!init.gpu_allocator.write_buffer(*projected_buffer, std::span{ zero_projected })) {
    std::println("Failed to zero-initialize projected buffer!");
    return false;
  }

  if (cpu_data) {
    auto const &splats = cpu_data->get();
    if (!init.gpu_allocator.write_buffer(*geometry_buffer, std::span{ splats.geometries })) {
      std::println("Failed to upload splat geometry!");
      return false;
    }

    if (!init.gpu_allocator.write_buffer(*appearance_buffer, std::span{ splats.appearances })) {
      std::println("Failed to upload splat appearance!");
      return false;
    }
    return true;
  }

  std::vector<GaussianGeometry> const zero_geometry(data.splat_count);
  std::vector<GaussianAppearance> const zero_appearance(data.splat_count);
  if (!init.gpu_allocator.write_buffer(*geometry_buffer, std::span{ zero_geometry })) {
    std::println("Failed to zero-initialize geometry buffer!");
    return false;
  }
  if (!init.gpu_allocator.write_buffer(*appearance_buffer, std::span{ zero_appearance })) {
    std::println("Failed to zero-initialize appearance buffer!");
    return false;
  }

  return true;
}

}// namespace vkgsplat
