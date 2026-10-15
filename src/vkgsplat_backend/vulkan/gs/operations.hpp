#ifndef VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP

#include <expected>
#include <vkgsplat_utility/error.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace vulkan {
  struct Context;
}
struct RenderData;

namespace gs {

  // Projection dispatch; `time_pass` writes GpuPass::kProjection timestamps (once per frame only).
  [[nodiscard]] auto RecordProjection(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer,
    bool time_pass) -> std::expected<void, Error>;

  [[nodiscard]] auto RecordPhaseACompute(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>;

  [[nodiscard]] auto RecordRasterization(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>;

}// namespace gs

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP
