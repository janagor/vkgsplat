#pragma once

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

[[nodiscard]] auto device_initialization(Init &init, bool enable_validation = false) -> std::expected<void, Error>;

}// namespace vkgsplat
