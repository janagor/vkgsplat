#ifndef VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/camera.hpp>

#include <cstddef>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

struct GsFrameParams
{
  Camera const *camera{};
  size_t image_index{};
  f64 aspect_ratio{};
};

void RecordGsPipeline(RenderData &data);

void UpdateGsFrameState(Init const &init, RenderData &data, GsFrameParams const &frame);

void EvalGsPipeline(Init &init, RenderData &data, VkCommandBuffer command_buffer);

void DestroyGsPipeline(RenderData &data);

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_PIPELINE_HPP
