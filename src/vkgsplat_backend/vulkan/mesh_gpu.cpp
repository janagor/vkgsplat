#include "mesh_gpu.hpp"

#include <algorithm>
#include <print>
#include <span>
#include <utility>

#include "app_state.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/push_constants.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void DestroySphereBuffers(vulkan::Context &context, RenderData &data)
{
  (void)context;
  data.geometry_buffer.reset();
  data.appearance_buffer.reset();
  data.projected_buffer.reset();
  data.projection_cameras_buffer.reset();
}

auto CreateSphereBuffers(vulkan::Context &context, RenderData &data, SplatCpuData const &cpu_data) -> bool
{
  DestroySphereBuffers(context, data);

  if (context.allocator == nullptr) {
    std::println("VMA allocator missing for gaussian buffers!");
    return false;
  }
  auto &allocator = *context.allocator;

  if (cpu_data.geometries.size() != cpu_data.appearances.size() || cpu_data.geometries.empty()) {
    std::println("Invalid splat CPU data!");
    return false;
  }
  data.splat_count = static_cast<u32>(cpu_data.geometries.size());

  auto const geometry_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianGeometry));
  auto const appearance_buffer_size = static_cast<VkDeviceSize>(data.splat_count * sizeof(gs::GaussianAppearance));
  VkDeviceSize const projected_view_size = static_cast<VkDeviceSize>(data.splat_count) * sizeof(gs::GaussianProjected);
  VkDeviceSize const max_range = context.device.physical_device.properties.limits.maxStorageBufferRange;
  if (projected_view_size > max_range) {
    std::println("Projected Gaussian data exceeds maxStorageBufferRange!");
    return false;
  }
  u32 const requested_batch_size =
    data.lfd_grid.at(0) == 1U && data.lfd_grid.at(1) == 1U ? 1U
    : std::clamp(data.lfd_projection_batch_size, 1U, kMaxProjectionBatchSize);
  u32 const max_batch_size = static_cast<u32>(std::min<VkDeviceSize>({ requested_batch_size,
    max_range / projected_view_size, context.device.physical_device.properties.limits.maxComputeWorkGroupCount[2] }));
  data.lfd_projection_batch_size = std::max(1U, max_batch_size);
  VkDeviceSize const projected_buffer_size = projected_view_size * data.lfd_projection_batch_size;

  auto geometry_buffer = vulkan::CreateStorageBuffer(allocator, geometry_buffer_size);
  auto appearance_buffer = vulkan::CreateStorageBuffer(allocator, appearance_buffer_size);
  auto projected_buffer = vulkan::CreateDeviceStorageBuffer(allocator, projected_buffer_size);
  if (!projected_buffer && data.lfd_projection_batch_size > 1U) {
    data.lfd_projection_batch_size = 1U;
    projected_buffer = vulkan::CreateDeviceStorageBuffer(allocator, projected_view_size);
  }
  auto cameras_buffer = vulkan::CreateDeviceStorageBuffer(
    allocator, static_cast<VkDeviceSize>(data.lfd_projection_batch_size) * sizeof(gs::ProjectPushConstants));
  if (!geometry_buffer || !appearance_buffer || !projected_buffer || !cameras_buffer) {
    std::println("Failed to create gaussian buffers!");
    return false;
  }

  data.geometry_buffer = std::move(*geometry_buffer);
  data.appearance_buffer = std::move(*appearance_buffer);
  data.projected_buffer = std::move(*projected_buffer);
  data.projection_cameras_buffer = std::move(*cameras_buffer);

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
