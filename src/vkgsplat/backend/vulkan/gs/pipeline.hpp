#ifndef VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

void RecordGsPipeline(RenderData &data);

void EvalGsPipeline(vulkan::Context &context, RenderData &data, VkCommandBuffer command_buffer);

void DestroyGsPipeline(RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP
