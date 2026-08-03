#pragma once

#include <string>
#include <vector>

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto ReadFile(const std::string &filename) -> std::vector<char>;

[[nodiscard]] auto CreateShaderModule(Init &init, std::vector<char> const &code) -> VkShaderModule;

}// namespace vkgsplat
