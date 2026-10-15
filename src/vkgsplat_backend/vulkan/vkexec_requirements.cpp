#include "vkexec_requirements.hpp"

#include <vkexec/vulkan_requirements.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

auto MakeVkexecRequirements() -> vkexec::vulkan_requirements
{
  vkexec::vulkan_requirements req;
  req.api_version_major = 1;
  req.api_version_minor = 4;

  req.device_extensions = {
    VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,
  };

  return req;
}

}// namespace vkgsplat::vulkan
