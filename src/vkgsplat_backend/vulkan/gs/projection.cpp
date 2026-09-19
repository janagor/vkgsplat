#include "gs/projection.hpp"

#include "app_state.hpp"
#include "gs/load_heap_pipeline.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <array>
#include <cstdint>
#include <print>
#include <span>
#include <string>
#include <utility>

#include <vkexec/barrier.hpp>
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
  return true;
}

void DispatchProjection(vulkan::Context const &context,
  RenderData const &data,
  ProjectPushConstants const &push_constants,
  VkCommandBuffer command_buffer)
{
  if (!data.project_algorithm || context.vkexec_context == nullptr) { return; }
  (void)vkexec::record_pass(*context.vkexec_context,
    command_buffer,
    data.project_algorithm->bind(),
    std::as_bytes(std::span{ &push_constants, 1 }),
    data.project_algorithm->groups_for(data.splat_count));
  vkexec::barrier::compute_read(command_buffer);
}

void DestroyProjection(vulkan::Context &context, RenderData &data)
{
  (void)context;
  data.project_algorithm.reset();
}

}// namespace vkgsplat::gs
