#pragma once

#include <expected>

#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto DeviceInitialization(Init &init, bool enable_validation = false) -> std::expected<void, Error>;

}// namespace vkgsplat
