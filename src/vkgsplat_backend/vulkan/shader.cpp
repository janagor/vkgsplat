#include "shader.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "vulkan/initializers.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

auto ReadFile(const std::string &filename) -> std::vector<char>
{
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    std::cout << "failed to open file!\n";
    return {};
  }

  auto const file_size = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(file_size);

  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(file_size));

  file.close();

  return buffer;
}

auto CreateShaderModule(vulkan::Context &context, std::vector<char> const &code) -> VkShaderModule
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto const code_span = std::span{ reinterpret_cast<u32 const *>(code.data()), code.size() / sizeof(u32) };
  auto const create_info = initializers::ShaderModuleCreateInfo(code_span);

  VkShaderModule shader_module = nullptr;
  if (context.disp.createShaderModule(&create_info, nullptr, &shader_module) != VK_SUCCESS) { return VK_NULL_HANDLE; }

  return shader_module;
}

}// namespace vkgsplat
