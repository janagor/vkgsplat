#include "device.hpp"

#include <expected>
#include <print>
#include <system_error>

#include "vulkan_bootstrap.hpp"
#include "vulkan_context.hpp"
#include "vulkan_platform.hpp"
#include <vkgsplat_utility/error.hpp>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

auto DeviceInitialization(Init &init, bool enable_validation) -> std::expected<void, Error>
{
  if (init.platform == nullptr) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::invalid_argument), "Platform is required" } };
  }

  VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{};
  descriptor_heap_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
  descriptor_heap_features.descriptorHeap = VK_TRUE;

  VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untyped_pointers_features{};
  untyped_pointers_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR;
  untyped_pointers_features.shaderUntypedPointers = VK_TRUE;

  VkPhysicalDeviceVulkan12Features features_12{};
  features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features_12.bufferDeviceAddress = VK_TRUE;

  VkPhysicalDeviceVulkan13Features features_13{};
  features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features_13.dynamicRendering = VK_TRUE;
  features_13.shaderDemoteToHelperInvocation = VK_TRUE;
  features_13.maintenance4 = VK_TRUE;

  vkb::InstanceBuilder instance_builder;
  instance_builder.require_api_version(1, 4, 0);
  if (enable_validation) {
    std::println("Vulkan validation layers enabled");
    instance_builder.enable_validation_layers().use_default_debug_messenger();
  } else {
    std::println("Vulkan validation layers disabled");
  }
  return VKBResultToExpected(instance_builder.build())
    .and_then([&](vkb::Instance const &instance) -> std::expected<vkb::PhysicalDevice, Error> {
      init.instance = instance;
      init.inst_disp = init.instance.make_table();

      auto surface = vulkan::CreateSurfaceFromNativeWindow(init.instance, init.platform->native_window());
      if (!surface) { return std::unexpected{ surface.error() }; }
      init.surface = *surface;

      vkb::PhysicalDeviceSelector phys_device_selector(init.instance);

      return VKBResultToExpected(phys_device_selector.set_surface(init.surface)
          .add_required_extension(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME)
          .add_required_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)
          .add_required_extension(VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME)
          .add_required_extension_features(descriptor_heap_features)
          .add_required_extension_features(untyped_pointers_features)
          .set_required_features_12(features_12)
          .set_required_features_13(features_13)
          .select());
    })
    .and_then([&](vkb::PhysicalDevice const &physical_device) -> std::expected<vkb::Device, Error> {
      vkb::DeviceBuilder const device_builder{ physical_device };

      return VKBResultToExpected(device_builder.build());
    })
    .and_then([&](vkb::Device const &device) -> std::expected<void, Error> {
      init.device = device;
      init.disp = init.device.make_table();

      VkPhysicalDeviceSubgroupProperties subgroup_props{};
      subgroup_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
      VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &subgroup_props,
        .properties = {},
      };
      init.inst_disp.getPhysicalDeviceProperties2(init.device.physical_device, &props2);
      std::println("Subgroup size={} operations={:#x} (radix sort needs arithmetic)",
        subgroup_props.subgroupSize,
        static_cast<unsigned>(subgroup_props.supportedOperations));

      // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
      init.write_resource_descriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
        vkGetDeviceProcAddr(init.device, "vkWriteResourceDescriptorsEXT"));
      init.write_sampler_descriptors = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>(
        vkGetDeviceProcAddr(init.device, "vkWriteSamplerDescriptorsEXT"));
      init.cmd_bind_resource_heap =
        reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(vkGetDeviceProcAddr(init.device, "vkCmdBindResourceHeapEXT"));
      init.cmd_bind_sampler_heap =
        reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>(vkGetDeviceProcAddr(init.device, "vkCmdBindSamplerHeapEXT"));
      init.cmd_push_data = reinterpret_cast<PFN_vkCmdPushDataEXT>(vkGetDeviceProcAddr(init.device, "vkCmdPushDataEXT"));
      // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

      if (init.write_resource_descriptors == nullptr || init.write_sampler_descriptors == nullptr
          || init.cmd_bind_resource_heap == nullptr || init.cmd_bind_sampler_heap == nullptr
          || init.cmd_push_data == nullptr) {
        return std::unexpected{ Error{ std::make_error_code(std::errc::function_not_supported),
          "VK_EXT_descriptor_heap entry points are unavailable" } };
      }

      return {};
    });
}

}// namespace vkgsplat
