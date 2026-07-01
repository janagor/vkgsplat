#pragma once

#include <string>
#include <vector>

#include "app_state.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

[[nodiscard]] auto read_file(const std::string &filename) -> std::vector<char>;

[[nodiscard]] auto create_shader_module(Init &init, std::vector<char> const &code) -> VkShaderModule;

}// namespace vkgsplat
