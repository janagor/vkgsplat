#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/camera.hpp>

#include <cstddef>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

struct GsFrameParams
{
  Camera const &camera;
  size_t image_index{};
  f64 aspect_ratio{};
};

void record_gs_pipeline(RenderData &data);

void update_gs_frame_state(Init const &init, RenderData &data, GsFrameParams const &frame);

void eval_gs_pipeline(Init &init, RenderData const &data, VkCommandBuffer command_buffer);

void destroy_gs_pipeline(RenderData &data);

}// namespace vkgsplat::gs
