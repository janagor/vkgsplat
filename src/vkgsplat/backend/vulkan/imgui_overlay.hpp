#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <expected>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto init_imgui_overlay(Init &init, RenderData &data) -> std::expected<void, Error>;

void shutdown_imgui_overlay(Init &init, RenderData &data);

void recreate_imgui_overlay_pipeline(Init &init, RenderData &data);

void record_imgui_overlay(Init &init, RenderData const &data, VkCommandBuffer command_buffer, size_t image_index);

void update_imgui_gpu_timings(RenderData &data);

}// namespace vkgsplat
