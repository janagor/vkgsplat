#ifndef VKGSPLAT_BACKEND_VULKAN_SCREENSHOT_HPP
#define VKGSPLAT_BACKEND_VULKAN_SCREENSHOT_HPP

#include <expected>
#include <string_view>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan {

// Reads the current raster color target (RGBA8) and writes a PNG. Call after at least one drawn frame.
[[nodiscard]] auto SaveColorTargetPng(Context &context, RenderData &data, std::string_view path)
  -> std::expected<void, Error>;

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_SCREENSHOT_HPP
