#include "gs/projection.hpp"

#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <array>
#include <cstdint>
#include <print>
#include <span>
#include <string>

#include <vkexec/barrier.hpp>
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
  return data.project_algorithm.init(context, shader_path, std::span{ specialization_constants });
}

void DispatchProjection(vulkan::Context const &context,
  RenderData const &data,
  ProjectPushConstants const &push_constants,
  VkCommandBuffer command_buffer)
{
  context.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.project_algorithm.pipeline());

  VkPushDataInfoEXT const push_info = {
    .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
    .pNext = nullptr,
    .offset = 0,
    .data = { .address = &push_constants, .size = sizeof(ProjectPushConstants) },
  };
  context.cmd_push_data(command_buffer, &push_info);

  uint32_t const workgroup_count = (data.splat_count + 63U) / 64U;
  context.disp.cmdDispatch(command_buffer, workgroup_count, 1U, 1U);
  vkexec::barrier::compute_read(command_buffer);
}

void DestroyProjection(vulkan::Context &context, RenderData &data) { data.project_algorithm.destroy(context); }

}// namespace vkgsplat::gs
