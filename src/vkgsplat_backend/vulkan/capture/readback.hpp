#ifndef VKGSPLAT_BACKEND_VULKAN_CAPTURE_READBACK_HPP
#define VKGSPLAT_BACKEND_VULKAN_CAPTURE_READBACK_HPP

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/frame.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan {

// Readback is called on the render worker after at least one completed frame.
[[nodiscard]] auto ReadColorTargetRgba8(Context &context, RenderData &data) -> std::expected<RgbaFrame, Error>;

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_CAPTURE_READBACK_HPP
