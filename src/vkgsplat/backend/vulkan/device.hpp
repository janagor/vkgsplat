#ifndef VKGSPLAT_BACKEND_VULKAN_DEVICE_HPP
#define VKGSPLAT_BACKEND_VULKAN_DEVICE_HPP

#include <expected>

#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto DeviceInitialization(Init &init, bool enable_validation = false) -> std::expected<void, Error>;

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_DEVICE_HPP
