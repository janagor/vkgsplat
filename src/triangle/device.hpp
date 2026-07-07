#pragma once

#include <expected>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include "error.hpp"

namespace vkgsplat {

[[nodiscard]] auto device_initialization(Init &init) -> std::expected<void, Error>;

}// namespace vkgsplat
