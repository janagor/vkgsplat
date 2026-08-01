#include "compute/op_algo_dispatch.hpp"
#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

void OpAlgoDispatch::record(Init const &init, [[maybe_unused]] RenderData const &data, VkCommandBuffer cmd)
{
  init.disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, algorithm_.pipeline());
  init.disp.cmdDispatch(cmd, workgroup_size_.at(0), workgroup_size_.at(1), workgroup_size_.at(2));

  VkMemoryBarrier const barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
  };

  init.disp.cmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    0,
    1,
    &barrier,
    0,
    nullptr,
    0,
    nullptr);
}

}// namespace vkgsplat::compute
