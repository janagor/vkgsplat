#include "gs/projection.hpp"

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <array>
#include <cstdint>
#include <print>
#include <span>
#include <string>

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

auto InitProjection(Init &init, RenderData &data) -> bool
{
  if (data.splat_count == 0) {
    std::println("Project gaussians requires non-zero splat_count!");
    return false;
  }

  std::array<uint32_t, 1> const specialization_constants{ data.splat_count };
  std::string const shader_path = std::string(SHADER_DIRECTORY) + "/projection.comp.spv";
  return data.project_algorithm.init(init, shader_path, std::span{ specialization_constants });
}

void DispatchProjection(Init const &init,
  RenderData const &data,
  ProjectPushConstants const &push_constants,
  VkCommandBuffer command_buffer)
{
  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.project_algorithm.pipeline());

  VkPushDataInfoEXT const push_info = {
    .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
    .pNext = nullptr,
    .offset = 0,
    .data = { .address = &push_constants, .size = sizeof(ProjectPushConstants) },
  };
  init.cmd_push_data(command_buffer, &push_info);

  uint32_t const workgroup_count = (data.splat_count + 63U) / 64U;
  init.disp.cmdDispatch(command_buffer, workgroup_count, 1U, 1U);

  VkMemoryBarrier const barrier = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
  };
  init.disp.cmdPipelineBarrier(command_buffer,
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

void DestroyProjection(Init &init, RenderData &data) { data.project_algorithm.destroy(init); }

}// namespace vkgsplat::gs
