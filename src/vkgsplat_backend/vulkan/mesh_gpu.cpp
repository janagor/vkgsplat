#include "mesh_gpu.hpp"

#include <print>
#include <span>
#include <utility>

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan/gpu_buffers.hpp"
#include <vkgsplat_io/splat_cpu.hpp>
#include "vulkan_context.hpp"
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void DestroySphereBuffers(vulkan::Context &context, RenderData &data)
{
  (void)context;
  data.geometry_buffer.reset();
  data.appearance_buffer.reset();
  data.projected_buffer.reset();
}

auto CreateSphereBuffers(vulkan::Context &context, RenderData &data, SplatCpuData const &cpu_data) -> bool
{
  DestroySphereBuffers(context, data);

  if (context.vkexec_context == nullptr) {
    std::println("vkexec context missing for gaussian buffers!");
    return false;
  }
  auto &vkexec = *context.vkexec_context;

  if (cpu_data.geometries.size() != cpu_data.appearances.size() || cpu_data.geometries.empty()) {
    std::println("Invalid splat CPU data!");
    return false;
  }
  data.splat_count = static_cast<u32>(cpu_data.geometries.size());

  auto const geometry_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianGeometry));
  auto const appearance_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianAppearance));
  auto const projected_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianProjected));

  auto geometry_buffer = vulkan::CreateStorageBuffer(vkexec, geometry_buffer_size);
  auto appearance_buffer = vulkan::CreateStorageBuffer(vkexec, appearance_buffer_size);
  auto projected_buffer = vulkan::CreateDeviceStorageBuffer(vkexec, projected_buffer_size);
  if (!geometry_buffer || !appearance_buffer || !projected_buffer) {
    std::println("Failed to create gaussian buffers!");
    return false;
  }

  data.geometry_buffer = std::move(*geometry_buffer);
  data.appearance_buffer = std::move(*appearance_buffer);
  data.projected_buffer = std::move(*projected_buffer);

  if (!vulkan::WriteMapped(*data.geometry_buffer, std::span{ cpu_data.geometries })) {
    std::println("Failed to upload splat geometry!");
    DestroySphereBuffers(context, data);
    return false;
  }

  if (!vulkan::WriteMapped(*data.appearance_buffer, std::span{ cpu_data.appearances })) {
    std::println("Failed to upload splat appearance!");
    DestroySphereBuffers(context, data);
    return false;
  }

  return true;
}

}// namespace vkgsplat
