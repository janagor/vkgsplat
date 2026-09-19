#ifndef VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace vulkan {
  struct Context;
}
struct RenderData;

namespace gs {

  // Projection dispatch; `time_pass` writes GpuPass::kProjection timestamps (once per frame only).
  void RecordProjection(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer,
    bool time_pass);

  void RecordPhaseACompute(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer);

  void RecordRasterization(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer);

}// namespace gs

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP
