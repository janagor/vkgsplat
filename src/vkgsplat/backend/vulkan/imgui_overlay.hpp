#pragma once

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <expected>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto InitImguiOverlay(Init &init, RenderData &data) -> std::expected<void, Error>;

void ShutdownImguiOverlay(Init &init, RenderData &data);

void RecreateImguiOverlayPipeline(Init &init, RenderData &data);

void RecordImguiOverlay(Init &init, RenderData const &data, VkCommandBuffer command_buffer, size_t image_index);

void UpdateImguiGpuTimings(RenderData &data);

}// namespace vkgsplat
