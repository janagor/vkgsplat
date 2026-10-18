#include "gs/projection.hpp"

#include "app_state.hpp"
#include "gs/load_heap_pipeline.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <print>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vkexec/barrier.hpp>
#include <vkexec/pass.hpp>
#include <vkexec_extensions/descriptor_heap/pass.hpp>
#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

auto InitProjection(vulkan::Context &context, RenderData &data) -> bool
{
  if (data.splat_count == 0) {
    std::println("Project gaussians requires non-zero splat_count!");
    return false;
  }

  std::array<uint32_t, 1> const specialization_constants{ data.splat_count };
  std::string const shader_path = std::string(kShaderDirectory) + "/projection.comp.spv";
  auto algorithm = LoadHeapAlgorithm(context, shader_path, std::span{ specialization_constants });
  if (!algorithm) { return false; }
  data.project_algorithm = std::move(*algorithm);
  if (data.lfd_projection_batch_size > 1U) {
    std::string const batch_shader_path = std::string(kShaderDirectory) + "/projection_batch.comp.spv";
    auto batch_algorithm = LoadHeapAlgorithm(context, batch_shader_path, std::span{ specialization_constants });
    if (batch_algorithm) {
      data.project_batch_algorithm = std::move(*batch_algorithm);
    } else {
      data.lfd_projection_batch_size = 1U;
    }
  }
  return true;
}

auto DispatchProjection(vulkan::Context const &context,
  RenderData const &data,
  ProjectPushConstants const &push_constants,
  VkCommandBuffer command_buffer) -> std::expected<void, Error>
{
  if (!data.project_algorithm || context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "projection is not initialized") };
  }
  auto recorded = vkexec::record_pass(*context.vkexec_context,
    command_buffer,
    data.project_algorithm->bind(),
    std::as_bytes(std::span{ &push_constants, 1 }),
    data.project_algorithm->groups_for(data.splat_count));
  if (!recorded) { return std::unexpected{ MakeError(std::errc::io_error, recorded.error().message()) }; }
  auto barrier = vkexec::barrier::compute_read(*context.vkexec_context, command_buffer);
  if (!barrier) { return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) }; }
  return {};
}

auto DispatchProjectionBatch(vulkan::Context const &context,
  RenderData const &data,
  std::span<ProjectPushConstants const> cameras,
  VkCommandBuffer command_buffer) -> std::expected<void, Error>
{
  if (!data.project_batch_algorithm || !data.projection_cameras_buffer || cameras.empty()
      || cameras.size() > data.lfd_projection_batch_size) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "batch projection is not initialized") };
  }
  auto const &limits = context.device.physical_device.properties.limits;
  constexpr u32 kProjectionLocalSize = 64U;
  u64 const groups = (static_cast<u64>(data.splat_count) + kProjectionLocalSize - 1U) / kProjectionLocalSize;
  u32 const group_count_x = static_cast<u32>(std::min<u64>(groups, limits.maxComputeWorkGroupCount[0]));
  u64 const group_count_y = (groups + group_count_x - 1U) / group_count_x;
  if (group_count_y > limits.maxComputeWorkGroupCount[1] || cameras.size() > limits.maxComputeWorkGroupCount[2]) {
    return std::unexpected{ MakeError(std::errc::value_too_large, "batch projection exceeds compute dispatch limits") };
  }
  VkMemoryBarrier const reuse_barrier{
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
  };
  context.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0U,
    1U,
    &reuse_barrier,
    0U,
    nullptr,
    0U,
    nullptr);
  context.disp.cmdUpdateBuffer(
    command_buffer, data.projection_cameras_buffer->handle(), 0, cameras.size_bytes(), cameras.data());
  if (auto barrier = vkexec::barrier::transfer_to_compute(*context.vkexec_context, command_buffer); !barrier) {
    return std::unexpected{ MakeError(std::errc::io_error, barrier.error().message()) };
  }
  ProjectBatchPushConstants const push{ .group_count_x = group_count_x };
  auto recorded = vkexec::record_pass(*context.vkexec_context,
    command_buffer,
    data.project_batch_algorithm->bind(),
    std::as_bytes(std::span{ &push, 1 }),
    vkexec::dispatch{
      .x = group_count_x, .y = static_cast<u32>(group_count_y), .z = static_cast<u32>(cameras.size()) });
  if (!recorded) { return std::unexpected{ MakeError(std::errc::io_error, recorded.error().message()) }; }
  return {};
}

void DestroyProjection(vulkan::Context &context, RenderData &data)
{
  (void)context;
  data.project_algorithm.reset();
  data.project_batch_algorithm.reset();
}

}// namespace vkgsplat::gs
