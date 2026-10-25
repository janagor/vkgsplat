#include "device.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "video/video_capabilities.hpp"
#include "video/video_device.hpp"
#include "vkexec_requirements.hpp"
#include "vulkan_bootstrap.hpp"
#include "vulkan_context.hpp"
#include "vulkan_platform.hpp"
#include <vkgsplat/driver.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/video_capabilities.hpp>
#include <vkgsplat_utility/error.hpp>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {
namespace {

  [[nodiscard]] auto CheckHeadlessExtension() -> std::expected<void, Error>
  {
    constexpr std::uint32_t kMaximumEnumerationAttempts = 4;
    std::vector<VkExtensionProperties> extensions;
    bool enumerated = false;
    for (std::uint32_t attempt = 0; attempt < kMaximumEnumerationAttempts; ++attempt) {
      std::uint32_t extension_count = 0;
      VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
      if (result != VK_SUCCESS) {
        return std::unexpected{ MakeError(std::errc::io_error, "Failed to enumerate Vulkan instance extensions") };
      }
      extensions.resize(extension_count);
      result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());
      if (result == VK_INCOMPLETE) { continue; }
      if (result != VK_SUCCESS) {
        return std::unexpected{ MakeError(std::errc::io_error, "Failed to enumerate Vulkan instance extensions") };
      }
      extensions.resize(extension_count);
      enumerated = true;
      break;
    }
    if (!enumerated) {
      return std::unexpected{ MakeError(
        std::errc::io_error, "Vulkan instance extension enumeration remained incomplete") };
    }
    bool const found = std::ranges::any_of(extensions, [](VkExtensionProperties const &extension) -> bool {
      return std::string_view{ static_cast<char const *>(extension.extensionName) }
             == VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME;
    });
    if (found) { return {}; }
    return std::unexpected{ MakeError(std::errc::function_not_supported,
      "Headless rendering unavailable: VK_EXT_headless_surface is not supported by this Vulkan implementation") };
  }

  // Query device + surface present-timing capabilities and enable only what both support.
  // Device features alone are insufficient: Mesa often advertises absolute/id2 features while
  // the surface only supports relative (Xwayland) or neither scheduling mode.
  void TryEnablePresentTiming(vulkan::Context &context, vkb::PhysicalDevice &physical_device)
  {
    // Optional as a device extension (often instance-only); still try so CreateDevice
    // dependency checks are satisfied when the ICD advertises it.
    static_cast<void>(physical_device.enable_extension_if_present(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME));
    if (!physical_device.enable_extension_if_present(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)
        && !physical_device.enable_extension_if_present(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
      std::println(stderr, "[present-timing] calibrated timestamps unavailable; --frame-rate ignored");
      return;
    }

    bool const present_id2_ext = physical_device.enable_extension_if_present(VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
    bool const present_timing_ext = physical_device.enable_extension_if_present(VK_EXT_PRESENT_TIMING_EXTENSION_NAME);
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

    bool const abs_ok =
      supported_timing.presentAtAbsoluteTime == VK_TRUE && timing_caps.presentAtAbsoluteTimeSupported == VK_TRUE;
    bool const rel_ok =
      supported_timing.presentAtRelativeTime == VK_TRUE && timing_caps.presentAtRelativeTimeSupported == VK_TRUE;
    bool const timing_surface_ok = timing_caps.presentTimingSupported == VK_TRUE;
    bool const id2_ok =
      present_id2_ext && supported_id2.presentId2 == VK_TRUE && id2_caps.presentId2Supported == VK_TRUE;

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
    std::println("[present-timing] features enabled (absolute={}, relative={}, presentId2={}, stageQueries={:#x})",
      context.present_at_absolute_time,
      context.present_at_relative_time,
      context.present_id2_enabled,
      static_cast<unsigned>(context.present_stage_queries));
  }

  auto InitializeDeviceContext(vulkan::Context &context, vkb::Device const &device) -> std::expected<void, Error>
  {
    context.device = device;
    context.disp = context.device.make_table();

    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    context.set_swapchain_present_timing_queue_size = reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT>(
      vkGetDeviceProcAddr(context.device, "vkSetSwapchainPresentTimingQueueSizeEXT"));
    context.get_swapchain_timing_properties = reinterpret_cast<PFN_vkGetSwapchainTimingPropertiesEXT>(
      vkGetDeviceProcAddr(context.device, "vkGetSwapchainTimingPropertiesEXT"));
    context.get_swapchain_time_domain_properties = reinterpret_cast<PFN_vkGetSwapchainTimeDomainPropertiesEXT>(
      vkGetDeviceProcAddr(context.device, "vkGetSwapchainTimeDomainPropertiesEXT"));
    context.get_past_presentation_timing = reinterpret_cast<PFN_vkGetPastPresentationTimingEXT>(
      vkGetDeviceProcAddr(context.device, "vkGetPastPresentationTimingEXT"));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

    if (context.present_timing_enabled
        && (context.set_swapchain_present_timing_queue_size == nullptr
            || context.get_swapchain_timing_properties == nullptr
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
  }

  [[nodiscard]] auto SelectRenderingDevice(vulkan::Context const &context,
    vkb::PhysicalDeviceSelector &phys_device_selector,
    VideoEncodeSupport video_encode) -> std::expected<vkb::PhysicalDevice, Error>
  {
    if (video_encode != VideoEncodeSupport::kRequired) { return VKBResultToExpected(phys_device_selector.select()); }
    auto candidates = VKBResultToExpected(phys_device_selector.select_devices());
    if (!candidates) { return std::unexpected(candidates.error()); }
    auto preferred = VKBResultToExpected(phys_device_selector.select());
    if (!preferred) { return std::unexpected(preferred.error()); }
    std::vector<vulkan::video::CandidateInfo> candidate_info;
    candidate_info.reserve(candidates->size());
    std::ranges::transform(*candidates,
      std::back_inserter(candidate_info),
      [&preferred](vkb::PhysicalDevice const &candidate) -> vulkan::video::CandidateInfo {
        return vulkan::video::CandidateInfo{
          .preferred_rendering_device = candidate.physical_device == preferred->physical_device,
          .device_type = candidate.properties.deviceType,
          .name = candidate.name,
        };
      });
    std::string unavailable_devices;
    for (auto const index : vulkan::video::RankCandidates(candidate_info)) {
      auto const &candidate = candidates->at(index);
      auto report = vulkan::video::ProbeH264Capabilities(context.inst_disp, candidate.physical_device);
      if (!report) { return std::unexpected(report.error()); }
      if (report->supports_offline_h264()) { return candidate; }
      if (!unavailable_devices.empty()) { unavailable_devices += "; "; }
      unavailable_devices += report->device_name + ": " + report->diagnostic();
    }
    return std::unexpected{ MakeError(std::errc::function_not_supported,
      "No rendering device supports required 8-bit 4:2:0 H.264 encoding and image formats. " + unavailable_devices) };
  }

  [[nodiscard]] auto CreateInstance(DriverConfig const &config, SurfaceMode mode) -> std::expected<vkb::Instance, Error>
  {
    auto const requirements = vulkan::MakeVkexecRequirements();
    bool const headless = mode == SurfaceMode::kHeadless;
    bool const offscreen = mode == SurfaceMode::kOffscreen;
    if (headless) {
      if (auto checked = CheckHeadlessExtension(); !checked) { return std::unexpected(checked.error()); }
    }

    vkb::InstanceBuilder builder;
    builder.require_api_version(requirements.api_version_major, requirements.api_version_minor, 0);
    if (headless || offscreen) { builder.set_headless(true); }
    if (headless) {
      builder.enable_extension(VK_KHR_SURFACE_EXTENSION_NAME);
      builder.enable_extension(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
    }
    if (config.request_present_timing && !offscreen) {
      builder.enable_extension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    }
    if (config.enable_validation) {
      std::println("Vulkan validation layers enabled");
      builder.enable_validation_layers().use_default_debug_messenger();
    } else {
      std::println("Vulkan validation layers disabled");
    }
    return VKBResultToExpected(builder.build());
  }

  [[nodiscard]] auto SelectPhysicalDevice(vulkan::Context &context,
    DriverConfig const &config,
    vkb::Instance const &instance,
    bool offscreen) -> std::expected<vkb::PhysicalDevice, Error>
  {
    auto const requirements = vulkan::MakeVkexecRequirements();
    context.instance = instance;
    context.inst_disp = context.instance.make_table();
    if (!offscreen) {
      auto surface = vulkan::CreateSurface(context.instance, *context.platform);
      if (!surface) { return std::unexpected{ surface.error() }; }
      context.surface = *surface;
    }

    vkb::PhysicalDeviceSelector selector(context.instance);
    if (!offscreen) { selector.set_surface(context.surface); }
    selector.require_present(!offscreen);
    selector.set_minimum_version(requirements.api_version_major, requirements.api_version_minor);
    for (char const *extension : requirements.device_extensions) { selector.add_required_extension(extension); }
    VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{};
    descriptor_heap_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
    descriptor_heap_features.descriptorHeap = VK_TRUE;
    selector.add_required_extension_features(descriptor_heap_features);

    VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untyped_pointers_features{};
    untyped_pointers_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR;
    untyped_pointers_features.shaderUntypedPointers = VK_TRUE;
    selector.add_required_extension_features(untyped_pointers_features);

    VkPhysicalDeviceVulkan12Features features_12{};
    features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features_12.bufferDeviceAddress = VK_TRUE;
    features_12.timelineSemaphore = VK_TRUE;
    selector.add_required_extension_features(features_12);

    if (config.require_shader_int64) {
      VkPhysicalDeviceFeatures shader_features{};
      shader_features.shaderInt64 = VK_TRUE;
      selector.set_required_features(shader_features);
    }

    VkPhysicalDeviceVulkan13Features features_13{};
    features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features_13.synchronization2 = VK_TRUE;
    features_13.dynamicRendering = VK_TRUE;
    features_13.shaderDemoteToHelperInvocation = VK_TRUE;
    features_13.maintenance4 = VK_TRUE;
    selector.add_required_extension_features(features_13);
    return SelectRenderingDevice(context, selector, config.video_encode);
  }

  [[nodiscard]] auto ConfigureVideoEncoding(vulkan::Context &context,
    VideoEncodeSupport support,
    vkb::PhysicalDevice &physical_device) -> std::expected<void, Error>
  {
    if (support == VideoEncodeSupport::kDisabled) { return {}; }
    auto report = vulkan::video::ProbeH264Capabilities(context.inst_disp, physical_device.physical_device);
    if (!report) { return std::unexpected(report.error()); }
    context.video_encode_capabilities.diagnostic = report->diagnostic();
    if (!report->supports_offline_h264()) {
      if (support == VideoEncodeSupport::kRequired) {
        return std::unexpected{ MakeError(std::errc::function_not_supported,
          context.video_encode_capabilities.diagnostic + "\nOffline H.264 recording requirements are unavailable") };
      }
      context.video_encode_capabilities.diagnostic += "\nStatus: H.264 unavailable";
      return {};
    }
    constexpr std::array kVideoExtensions{
      VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
      VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
      VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
    };
    if (!physical_device.enable_extensions_if_present(kVideoExtensions.size(), kVideoExtensions.data())) {
      return std::unexpected{ MakeError(
        std::errc::function_not_supported, "H.264 extensions became unavailable during device creation") };
    }
    auto const selected = report->select_offline_h264();
    auto const queue_family_index = vulkan::video::ChooseEncodeQueue(*report);
    if (!selected || !queue_family_index) {
      return std::unexpected{ MakeError(
        std::errc::state_not_recoverable, "usable H.264 device has no selected configuration or encode queue") };
    }
    context.video_encode_capabilities.max_width = selected->max_extent.width;
    context.video_encode_capabilities.max_height = selected->max_extent.height;
    context.video_encode_capabilities.capture_conversion_supported = selected->capture_conversion_supported;
    context.video_encode_capabilities.reference_pictures_supported = selected->reference_pictures_supported;
    context.video_encode.emplace(vulkan::video::EncodeDevice{
      .queue = VK_NULL_HANDLE,
      .queue_family_index = *queue_family_index,
      .selection = *selected,
      .capabilities = std::move(*report),
    });
    return {};
  }

  [[nodiscard]] auto CreateLogicalDevice(vulkan::Context &context,
    DriverConfig const &config,
    vkb::PhysicalDevice physical_device,
    bool offscreen) -> std::expected<vkb::Device, Error>
  {
    if (config.request_present_timing && !offscreen) { TryEnablePresentTiming(context, physical_device); }
    if (auto configured = ConfigureVideoEncoding(context, config.video_encode, physical_device); !configured) {
      return std::unexpected(configured.error());
    }
    vkb::DeviceBuilder const builder{ physical_device };
    return VKBResultToExpected(builder.build());
  }

  [[nodiscard]] auto FinalizeDeviceContext(vulkan::Context &context, vkb::Device const &device)
    -> std::expected<void, Error>
  {
    if (auto initialized = InitializeDeviceContext(context, device); !initialized) { return initialized; }
    if (!context.video_encode) { return {}; }
    vkGetDeviceQueue(device.device, context.video_encode->queue_family_index, 0, &context.video_encode->queue);
    if (context.video_encode->queue == VK_NULL_HANDLE) {
      return std::unexpected{ MakeError(std::errc::state_not_recoverable, "H.264 encode queue was not created") };
    }
    context.video_encode_capabilities.h264_enabled = true;
    context.video_encode_capabilities.diagnostic += "\nStatus: H.264 enabled";
    return {};
  }
}// namespace

// This function coordinates instance, physical-device, logical-device, and optional video setup.
auto DeviceInitialization(vulkan::Context &context, DriverConfig const &config) -> std::expected<void, Error>
{
  if (config.video_encode == VideoEncodeSupport::kDisabled) {
    context.video_encode_capabilities.diagnostic = "Vulkan Video initialization disabled";
  }
  if (context.platform == nullptr) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::invalid_argument), "Platform is required" } };
  }
  bool const offscreen = context.platform->surface_mode() == SurfaceMode::kOffscreen;
  auto instance = CreateInstance(config, context.platform->surface_mode());
  if (!instance) { return std::unexpected(instance.error()); }
  auto physical_device = SelectPhysicalDevice(context, config, *instance, offscreen);
  if (!physical_device) { return std::unexpected(physical_device.error()); }
  auto device = CreateLogicalDevice(context, config, std::move(*physical_device), offscreen);
  if (!device) { return std::unexpected(device.error()); }
  if (auto finalized = FinalizeDeviceContext(context, *device); !finalized) { return finalized; }
  context.shader_int64_enabled = config.require_shader_int64;
  return {};
}

}// namespace vkgsplat
