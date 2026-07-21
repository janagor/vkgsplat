#pragma once

#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

inline void pipeline_memory_barrier(Init const &init,
  VkCommandBuffer command_buffer,
  VkPipelineStageFlags src_stage,
  VkPipelineStageFlags dst_stage,
  VkAccessFlags src_access,
  VkAccessFlags dst_access)
{
  VkMemoryBarrier const barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = src_access,
    .dstAccessMask = dst_access,
  };
  init.disp.cmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

inline void transfer_to_compute_barrier(Init const &init, VkCommandBuffer command_buffer)
{
  pipeline_memory_barrier(init,
    command_buffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

inline void compute_to_compute_barrier(Init const &init, VkCommandBuffer command_buffer)
{
  pipeline_memory_barrier(init,
    command_buffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_ACCESS_SHADER_WRITE_BIT,
    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

inline void compute_read_barrier(Init const &init, VkCommandBuffer command_buffer)
{
  pipeline_memory_barrier(init,
    command_buffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_ACCESS_SHADER_WRITE_BIT,
    VK_ACCESS_SHADER_READ_BIT);
}

}// namespace vkgsplat::compute
