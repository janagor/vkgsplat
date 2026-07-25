#include "backend/vulkan/sync_objects/barrier.hpp"

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void Barrier::memory(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer, MemoryBarrierParams params)
{
  VkMemoryBarrier const barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = params.src_access,
    .dstAccessMask = params.dst_access,
  };
  disp.cmdPipelineBarrier(command_buffer, params.src_stage, params.dst_stage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void Barrier::transfer_to_compute(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer)
{
  memory(disp,
    command_buffer,
    {
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    });
}

void Barrier::compute_to_compute(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer)
{
  // NOLINTBEGIN(hicpp-signed-bitwise)
  memory(disp,
    command_buffer,
    {
      .src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      // DRAW_INDIRECT covers vkCmdDispatchIndirect reads of GPU-written args.
      .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
      .src_access = VK_ACCESS_SHADER_WRITE_BIT,
      .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
    });
  // NOLINTEND(hicpp-signed-bitwise)
}

void Barrier::compute_read(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer)
{
  memory(disp,
    command_buffer,
    {
      .src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      .src_access = VK_ACCESS_SHADER_WRITE_BIT,
      .dst_access = VK_ACCESS_SHADER_READ_BIT,
    });
}

}// namespace vkgsplat
