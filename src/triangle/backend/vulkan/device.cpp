#include "device.hpp"

#include <expected>
#include <system_error>

#include "error.hpp"
#include "vulkan_context.hpp"
#include "backend/vulkan/vulkan_bootstrap.hpp"
#include "window.hpp"

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

auto device_initialization(Init &init) -> std::expected<void, Error>
{
  init.window = create_window_glfw("Vulkan Triangle", true);

  VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{};
  descriptor_heap_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
  descriptor_heap_features.descriptorHeap = VK_TRUE;

  VkPhysicalDeviceVulkan12Features features_12{};
  features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features_12.bufferDeviceAddress = VK_TRUE;

  VkPhysicalDeviceVulkan13Features features_13{};
  features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features_13.dynamicRendering = VK_TRUE;

  vkb::InstanceBuilder instance_builder;
  return VKBResultToExpected(
    instance_builder.use_default_debug_messenger().request_validation_layers().require_api_version(1, 4, 0).build())
    .and_then([&](vkb::Instance const &instance) {
      init.instance = instance;
      init.inst_disp = init.instance.make_table();

      init.surface = create_surface_glfw(init.instance, init.window);

      vkb::PhysicalDeviceSelector phys_device_selector(init.instance);

      return VKBResultToExpected(phys_device_selector.set_surface(init.surface)
          .add_required_extension("VK_EXT_descriptor_heap")
          .add_required_extension("VK_KHR_buffer_device_address")
          .add_required_extension_features(descriptor_heap_features)
          .set_required_features_12(features_12)
          .set_required_features_13(features_13)
          .select());
    })
    .and_then([&](vkb::PhysicalDevice const &physical_device) {
      vkb::DeviceBuilder const device_builder{ physical_device };

      return VKBResultToExpected(device_builder.build());
    })
    .and_then([&](vkb::Device const &device) -> std::expected<void, Error> {
      init.device = device;
      init.disp = init.device.make_table();

      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      init.write_resource_descriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
        vkGetDeviceProcAddr(init.device, "vkWriteResourceDescriptorsEXT"));
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      init.cmd_bind_resource_heap = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(
        vkGetDeviceProcAddr(init.device, "vkCmdBindResourceHeapEXT"));

      if (init.write_resource_descriptors == nullptr || init.cmd_bind_resource_heap == nullptr) {
        return std::unexpected{ Error{ std::make_error_code(std::errc::function_not_supported),
          "VK_EXT_descriptor_heap entry points are unavailable" } };
      }

      return {};
    });
}

}// namespace vkgsplat
