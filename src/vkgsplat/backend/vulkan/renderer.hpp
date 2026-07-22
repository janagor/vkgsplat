#pragma once

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto get_queues(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto create_command_resources(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto create_sync_objects(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto recreate_swapchain(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto draw_frame(Init &init, RenderData &data, Camera const &camera) -> std::expected<void, Error>;

void cleanup(Init &init, RenderData &data);

}// namespace vkgsplat
