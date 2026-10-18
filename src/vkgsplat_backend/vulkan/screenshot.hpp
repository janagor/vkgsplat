#ifndef VKGSPLAT_BACKEND_VULKAN_SCREENSHOT_HPP
#define VKGSPLAT_BACKEND_VULKAN_SCREENSHOT_HPP

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan {

// Readback is called on the render worker after at least one completed frame.
[[nodiscard]] auto ReadColorTargetRgba8(Context &context, RenderData &data) -> std::expected<RgbaFrame, Error>;

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_SCREENSHOT_HPP
