#ifndef VKGSPLAT_BACKEND_VULKAN_SHADER_HPP
#define VKGSPLAT_BACKEND_VULKAN_SHADER_HPP

#include <string>
#include <vector>

namespace vkgsplat {

[[nodiscard]] auto ReadFile(const std::string &filename) -> std::vector<char>;

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_SHADER_HPP
