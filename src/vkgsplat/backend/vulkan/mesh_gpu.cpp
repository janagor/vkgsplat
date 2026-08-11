#include "mesh_gpu.hpp"

#include <print>
#include <span>

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include <vkgsplat_io/splat_cpu.hpp>
#include "vulkan_context.hpp"
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void DestroySphereBuffers(vulkan::Context &context, RenderData &data)
{
  context.gpu_allocator.destroy_buffer(data.geometry_buffer);
  context.gpu_allocator.destroy_buffer(data.appearance_buffer);
  context.gpu_allocator.destroy_buffer(data.projected_buffer);
  data.geometry_buffer = {};
  data.appearance_buffer = {};
  data.projected_buffer = {};
}

auto CreateSphereBuffers(vulkan::Context &context, RenderData &data, SplatCpuData const &cpu_data) -> bool
{
  DestroySphereBuffers(context, data);

  if (cpu_data.geometries.size() != cpu_data.appearances.size() || cpu_data.geometries.empty()) {
    std::println("Invalid splat CPU data!");
    return false;
  }
  data.splat_count = static_cast<u32>(cpu_data.geometries.size());

  auto const geometry_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianGeometry));
  auto const appearance_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianAppearance));
  auto const projected_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianProjected));

  auto geometry_buffer = context.gpu_allocator.create_storage_buffer(geometry_buffer_size);
  auto appearance_buffer = context.gpu_allocator.create_storage_buffer(appearance_buffer_size);
  auto projected_buffer = context.gpu_allocator.create_device_storage_buffer(projected_buffer_size);
  if (!geometry_buffer || !appearance_buffer || !projected_buffer) {
    std::println("Failed to create gaussian buffers!");
    return false;
  }

  data.geometry_buffer = *geometry_buffer;
  data.appearance_buffer = *appearance_buffer;
  data.projected_buffer = *projected_buffer;

  if (!context.gpu_allocator.write_buffer(*geometry_buffer, std::span{ cpu_data.geometries })) {
    std::println("Failed to upload splat geometry!");
    return false;
  }

  if (!context.gpu_allocator.write_buffer(*appearance_buffer, std::span{ cpu_data.appearances })) {
    std::println("Failed to upload splat appearance!");
    return false;
  }

  return true;
}

}// namespace vkgsplat
