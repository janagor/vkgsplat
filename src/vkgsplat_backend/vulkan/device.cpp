#include "device.hpp"

#include <expected>
#include <print>
#include <system_error>

#include "vulkan_bootstrap.hpp"
#include "vulkan_context.hpp"
#include "vulkan_platform.hpp"
#include <vkgsplat/driver.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {
namespace {

  // Query device + surface present-timing capabilities and enable only what both support.
  // Device features alone are insufficient: Mesa often advertises absolute/id2 features while
  // the surface only supports relative (Xwayland) or neither scheduling mode.
  void TryEnablePresentTiming(vulkan::Context &context, vkb::PhysicalDevice &physical_device)
  {
    // Optional as a device extension (often instance-only); still try so CreateDevice
    // dependency checks are satisfied when the ICD advertises it.
    static_cast<void>(
      physical_device.enable_extension_if_present(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME));
    if (!physical_device.enable_extension_if_present(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)
        && !physical_device.enable_extension_if_present(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
      std::println(stderr, "[present-timing] calibrated timestamps unavailable; --frame-rate ignored");
      return;
    }

    bool const present_id2_ext = physical_device.enable_extension_if_present(VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
    bool const present_timing_ext =
      physical_device.enable_extension_if_present(VK_EXT_PRESENT_TIMING_EXTENSION_NAME);
    if (!present_timing_ext) {
      std::println(stderr, "[present-timing] VK_EXT_present_timing unavailable; --frame-rate ignored");
      return;
    }

    VkPhysicalDevicePresentTimingFeaturesEXT supported_timing{};
    supported_timing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT;
    VkPhysicalDevicePresentId2FeaturesKHR supported_id2{};
    supported_id2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR;
    supported_id2.pNext = &supported_timing;
    VkPhysicalDeviceFeatures2 supported_features2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supported_id2,
      .features = {},
    };
    context.inst_disp.getPhysicalDeviceFeatures2(physical_device.physical_device, &supported_features2);

    VkPresentTimingSurfaceCapabilitiesEXT timing_caps{};
    timing_caps.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT;
    VkSurfaceCapabilitiesPresentId2KHR id2_caps{};
    id2_caps.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR;
    id2_caps.pNext = &timing_caps;
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    VkSurfaceCapabilities2KHR caps2{};
    caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
    caps2.pNext = present_id2_ext ? static_cast<void *>(&id2_caps) : static_cast<void *>(&timing_caps);
    VkPhysicalDeviceSurfaceInfo2KHR const surface_info{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
      .pNext = nullptr,
      .surface = context.surface,
    };
    if (context.inst_disp.getPhysicalDeviceSurfaceCapabilities2KHR(
          physical_device.physical_device, &surface_info, &caps2)
        != VK_SUCCESS) {
      std::println(stderr, "[present-timing] surface capability query failed; --frame-rate ignored");
      return;
    }

    bool const abs_ok = supported_timing.presentAtAbsoluteTime == VK_TRUE
                        && timing_caps.presentAtAbsoluteTimeSupported == VK_TRUE;
    bool const rel_ok = supported_timing.presentAtRelativeTime == VK_TRUE
                        && timing_caps.presentAtRelativeTimeSupported == VK_TRUE;
    bool const timing_surface_ok = timing_caps.presentTimingSupported == VK_TRUE;
    bool const id2_ok = present_id2_ext && supported_id2.presentId2 == VK_TRUE
                        && id2_caps.presentId2Supported == VK_TRUE;

    if (supported_timing.presentTiming != VK_TRUE || !timing_surface_ok || (!abs_ok && !rel_ok)) {
      std::println(stderr,
        "[present-timing] present-at-time unsupported on this surface "
        "(timing={}, abs={}, rel={}); --frame-rate ignored",
        timing_surface_ok,
        abs_ok,
        rel_ok);
      return;
    }

    // Prefer relative when available: Mesa Xwayland/Wayland often lack absolute support.
    bool const use_relative = rel_ok;
    bool const use_absolute = abs_ok && !use_relative;

    VkPhysicalDevicePresentTimingFeaturesEXT enable_timing{};
    enable_timing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT;
    enable_timing.presentTiming = VK_TRUE;
    enable_timing.presentAtAbsoluteTime = use_absolute ? VK_TRUE : VK_FALSE;
    enable_timing.presentAtRelativeTime = use_relative ? VK_TRUE : VK_FALSE;

    if (!physical_device.enable_extension_features_if_present(enable_timing)) {
      std::println(stderr, "[present-timing] failed to enable present-timing features; --frame-rate ignored");
      return;
    }

    if (id2_ok) {
      VkPhysicalDevicePresentId2FeaturesKHR enable_id2{};
      enable_id2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR;
      enable_id2.presentId2 = VK_TRUE;
      if (!physical_device.enable_extension_features_if_present(enable_id2)) {
        std::println(stderr, "[present-timing] presentId2 feature enable failed; continuing without present ids");
      } else {
        context.present_id2_enabled = true;
      }
    }

    context.present_at_absolute_time = use_absolute;
    context.present_at_relative_time = use_relative;
    context.present_stage_queries = timing_caps.presentStageQueries;
    context.present_timing_enabled = true;
    std::println(
      "[present-timing] features enabled (absolute={}, relative={}, presentId2={}, stageQueries={:#x})",
      context.present_at_absolute_time,
      context.present_at_relative_time,
      context.present_id2_enabled,
      static_cast<unsigned>(context.present_stage_queries));
  }

}// namespace

auto DeviceInitialization(vulkan::Context &context, DriverConfig const &config) -> std::expected<void, Error>
{
  if (context.platform == nullptr) {
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
  features_12.timelineSemaphore = VK_TRUE;

  VkPhysicalDeviceVulkan13Features features_13{};
  features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features_13.dynamicRendering = VK_TRUE;
  features_13.shaderDemoteToHelperInvocation = VK_TRUE;
  features_13.maintenance4 = VK_TRUE;

  vkb::InstanceBuilder instance_builder;
  instance_builder.require_api_version(1, 4, 0);
  if (config.request_present_timing) {
    // Required to query present-timing / present-id2 surface capabilities.
    instance_builder.enable_extension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
  }
  if (config.enable_validation) {
    std::println("Vulkan validation layers enabled");
    instance_builder.enable_validation_layers().use_default_debug_messenger();
  } else {
    std::println("Vulkan validation layers disabled");
  }
  return VKBResultToExpected(instance_builder.build())
    .and_then([&](vkb::Instance const &instance) -> std::expected<vkb::PhysicalDevice, Error> {
      context.instance = instance;
      context.inst_disp = context.instance.make_table();

      auto surface = vulkan::CreateSurfaceFromNativeWindow(context.instance, context.platform->native_window());
      if (!surface) { return std::unexpected{ surface.error() }; }
      context.surface = *surface;

      vkb::PhysicalDeviceSelector phys_device_selector(context.instance);

      return VKBResultToExpected(phys_device_selector.set_surface(context.surface)
          .add_required_extension(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME)
          .add_required_extension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)
          .add_required_extension(VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME)
          .add_required_extension_features(descriptor_heap_features)
          .add_required_extension_features(untyped_pointers_features)
          .set_required_features_12(features_12)
          .set_required_features_13(features_13)
          .select());
    })
    .and_then([&](vkb::PhysicalDevice physical_device) -> std::expected<vkb::Device, Error> {
      if (config.request_present_timing) { TryEnablePresentTiming(context, physical_device); }

      vkb::DeviceBuilder const device_builder{ physical_device };
      return VKBResultToExpected(device_builder.build());
    })
    .and_then([&](vkb::Device const &device) -> std::expected<void, Error> {
      context.device = device;
      context.disp = context.device.make_table();

      VkPhysicalDeviceSubgroupProperties subgroup_props{};
      subgroup_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
      VkPhysicalDeviceProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &subgroup_props,
        .properties = {},
      };
      context.inst_disp.getPhysicalDeviceProperties2(context.device.physical_device, &props2);
      std::println("Subgroup size={} operations={:#x} (radix sort needs arithmetic)",
        subgroup_props.subgroupSize,
        static_cast<unsigned>(subgroup_props.supportedOperations));

      // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
      context.write_resource_descriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
        vkGetDeviceProcAddr(context.device, "vkWriteResourceDescriptorsEXT"));
      context.write_sampler_descriptors = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>(
        vkGetDeviceProcAddr(context.device, "vkWriteSamplerDescriptorsEXT"));
      context.cmd_bind_resource_heap =
        reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(vkGetDeviceProcAddr(context.device, "vkCmdBindResourceHeapEXT"));
      context.cmd_bind_sampler_heap =
        reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>(vkGetDeviceProcAddr(context.device, "vkCmdBindSamplerHeapEXT"));
      context.cmd_push_data = reinterpret_cast<PFN_vkCmdPushDataEXT>(vkGetDeviceProcAddr(context.device, "vkCmdPushDataEXT"));

      context.set_swapchain_present_timing_queue_size = reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT>(
        vkGetDeviceProcAddr(context.device, "vkSetSwapchainPresentTimingQueueSizeEXT"));
      context.get_swapchain_timing_properties = reinterpret_cast<PFN_vkGetSwapchainTimingPropertiesEXT>(
        vkGetDeviceProcAddr(context.device, "vkGetSwapchainTimingPropertiesEXT"));
      context.get_swapchain_time_domain_properties = reinterpret_cast<PFN_vkGetSwapchainTimeDomainPropertiesEXT>(
        vkGetDeviceProcAddr(context.device, "vkGetSwapchainTimeDomainPropertiesEXT"));
      context.get_past_presentation_timing = reinterpret_cast<PFN_vkGetPastPresentationTimingEXT>(
        vkGetDeviceProcAddr(context.device, "vkGetPastPresentationTimingEXT"));
      // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

      if (context.write_resource_descriptors == nullptr || context.write_sampler_descriptors == nullptr
          || context.cmd_bind_resource_heap == nullptr || context.cmd_bind_sampler_heap == nullptr
          || context.cmd_push_data == nullptr) {
        return std::unexpected{ Error{ std::make_error_code(std::errc::function_not_supported),
          "VK_EXT_descriptor_heap entry points are unavailable" } };
      }

      if (context.present_timing_enabled
          && (context.set_swapchain_present_timing_queue_size == nullptr || context.get_swapchain_timing_properties == nullptr
              || context.get_swapchain_time_domain_properties == nullptr
              || context.get_past_presentation_timing == nullptr)) {
        std::println(stderr, "[present-timing] entry points unavailable; --frame-rate ignored");
        context.present_timing_enabled = false;
        context.present_id2_enabled = false;
        context.present_at_absolute_time = false;
        context.present_at_relative_time = false;
        context.present_stage_queries = 0;
      }

      return {};
    });
}

}// namespace vkgsplat
