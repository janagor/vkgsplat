#ifndef VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <expected>
#include <vkgsplat_utility/error.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

void RecordGsPipeline(RenderData &data);

[[nodiscard]] auto EvalGsPipeline(vulkan::Context &context, RenderData &data, VkCommandBuffer command_buffer)
  -> std::expected<void, Error>;

void DestroyGsPipeline(RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP
