#pragma once

#include <expected>

#include "app_state.hpp"
#include "error.hpp"

namespace vkgsplat {

[[nodiscard]] auto create_swapchain(Init &init) -> std::expected<void, Error>;

[[nodiscard]] auto create_swapchain_images(Init &init, RenderData &data) -> int;

[[nodiscard]] auto recreate_swapchain(Init &init, RenderData &data) -> int;

}// namespace vkgsplat
