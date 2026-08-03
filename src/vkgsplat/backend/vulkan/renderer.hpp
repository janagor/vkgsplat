#pragma once

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto GetQueues(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto CreateCommandResources(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto CreateSyncObjects(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto RecreateSwapchain(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto DrawFrame(Init &init, RenderData &data, Camera const &camera) -> std::expected<void, Error>;

void Cleanup(Init &init, RenderData &data);

}// namespace vkgsplat
