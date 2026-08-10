#ifndef VKGSPLAT_BACKEND_VULKAN_RENDERER_HPP
#define VKGSPLAT_BACKEND_VULKAN_RENDERER_HPP

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto GetQueues(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto CreateCommandResources(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto CreateSyncObjects(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>;

[[nodiscard]] auto RecreateSwapchain(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>;

void Cleanup(vulkan::Context &context, RenderData &data);

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_RENDERER_HPP
