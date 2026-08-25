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

  VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{};
  descriptor_heap_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
  descriptor_heap_features.descriptorHeap = VK_TRUE;
  req.require_extension_feature(descriptor_heap_features);

  VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untyped_pointers_features{};
  untyped_pointers_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR;
  untyped_pointers_features.shaderUntypedPointers = VK_TRUE;
  req.require_extension_feature(untyped_pointers_features);

  VkPhysicalDeviceVulkan12Features features_12{};
  features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features_12.bufferDeviceAddress = VK_TRUE;
  features_12.timelineSemaphore = VK_TRUE;
  req.require_extension_feature(features_12);

  VkPhysicalDeviceVulkan13Features features_13{};
  features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features_13.dynamicRendering = VK_TRUE;
  features_13.shaderDemoteToHelperInvocation = VK_TRUE;
  features_13.maintenance4 = VK_TRUE;
  req.require_extension_feature(features_13);

  return req;
}

}// namespace vkgsplat::vulkan
