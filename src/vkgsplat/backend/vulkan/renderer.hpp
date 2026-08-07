#ifndef VKGSPLAT_BACKEND_VULKAN_RENDERER_HPP
#define VKGSPLAT_BACKEND_VULKAN_RENDERER_HPP

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto GetQueues(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto CreateCommandResources(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto CreateSyncObjects(Init &init, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto RecreateSwapchain(Init &init, RenderData &data) -> std::expected<void, Error>;

void Cleanup(Init &init, RenderData &data);

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_RENDERER_HPP
