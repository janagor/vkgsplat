#include "video_capabilities.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include <VkBootstrapDispatch.h>
#include <vk_video/vulkan_video_codec_h264std.h>
#include <vk_video/vulkan_video_codec_h264std_encode.h>
#include <vulkan/vulkan_core.h>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan::video {
namespace {

  constexpr std::uint32_t kMaximumEnumerationAttempts = 4;
  constexpr std::array kRequiredExtensions{
    VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
  };
  constexpr std::array kProfiles{
    STD_VIDEO_H264_PROFILE_IDC_MAIN,
    STD_VIDEO_H264_PROFILE_IDC_BASELINE,
    STD_VIDEO_H264_PROFILE_IDC_HIGH,
  };

  [[nodiscard]] auto QueryError(std::string_view operation, VkResult result = VK_SUCCESS) -> Error
  {
    auto message = std::string{ operation } + " failed";
    if (result != VK_SUCCESS) { message += " (VkResult " + std::to_string(static_cast<int>(result)) + ")"; }
    return MakeError(std::errc::io_error, message);
  }

  [[nodiscard]] auto UnsupportedProfile(VkResult result) noexcept -> bool
  {
    return result == VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR
           || result == VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR
           || result == VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR;
  }

  [[nodiscard]] auto ExtentFor(Profile const &profile, ImageFormat const &format) noexcept -> VkExtent2D
  {
    VkExtent2D const maximum{
      .width = std::min(profile.capabilities.maxCodedExtent.width, format.max_extent.width),
      .height = std::min(profile.capabilities.maxCodedExtent.height, format.max_extent.height),
    };
    // encodeInputPictureGranularity affects edge texels, but does not impose a
    // valid-usage alignment requirement on the coded extent.
    if (profile.capabilities.minCodedExtent.width > maximum.width
        || profile.capabilities.minCodedExtent.height > maximum.height) {
      return {};
    }
    return maximum;
  }

  [[nodiscard]] auto ProfileName(StdVideoH264ProfileIdc profile) noexcept -> std::string_view
  {
    switch (profile) {
    case STD_VIDEO_H264_PROFILE_IDC_BASELINE:
      return "Baseline";
    case STD_VIDEO_H264_PROFILE_IDC_MAIN:
      return "Main";
    case STD_VIDEO_H264_PROFILE_IDC_HIGH:
      return "High";
    default:
      return "Unknown";
    }
  }

  [[nodiscard]] auto FormatName(VkFormat format) -> std::string
  {
    switch (format) {
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      return "VK_FORMAT_G8_B8R8_2PLANE_420_UNORM";
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return "VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM";
    default:
      return "VkFormat(" + std::to_string(static_cast<int>(format)) + ")";
    }
  }

  [[nodiscard]] auto EnumerateExtensions(Queries const &queries, VkPhysicalDevice device)
    -> std::expected<std::vector<VkExtensionProperties>, Error>
  {
    for (std::uint32_t attempt = 0; attempt < kMaximumEnumerationAttempts; ++attempt) {
      std::uint32_t count = 0;
      auto const count_result = queries.extensions(device, &count, nullptr);
      if (count_result != VK_SUCCESS) {
        return std::unexpected(QueryError("Vulkan device extension enumeration", count_result));
      }
      std::vector<VkExtensionProperties> extensions(count);
      auto const result = queries.extensions(device, &count, extensions.data());
      if (result == VK_INCOMPLETE) { continue; }
      if (result != VK_SUCCESS) { return std::unexpected(QueryError("Vulkan device extension enumeration", result)); }
      extensions.resize(count);
      return extensions;
    }
    return std::unexpected(QueryError("Vulkan device extension enumeration remained incomplete"));
  }

  [[nodiscard]] auto SupportsImageUsage(Queries const &queries,
    VkPhysicalDevice device,
    VkVideoProfileListInfoKHR const &profile_list,
    VkVideoFormatPropertiesKHR const &entry,
    VkImageUsageFlags usage,
    VkImageUsageFlags extra,
    VkImageFormatProperties2 *properties) -> std::expected<bool, Error>
  {
    if ((entry.imageUsageFlags & extra) != extra) { return false; }
    VkPhysicalDeviceImageFormatInfo2 const image_info{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = &profile_list,
      .format = entry.format,
      .type = entry.imageType,
      .tiling = entry.imageTiling,
      .usage = usage | extra,
      .flags = 0,
    };
    VkImageFormatProperties2 scratch{};
    scratch.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    if (properties == nullptr) { properties = &scratch; }
    auto const result = queries.image_format(device, &image_info, properties);
    if (result == VK_SUCCESS) { return true; }
    if (result == VK_ERROR_FORMAT_NOT_SUPPORTED || result == VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR) { return false; }
    return std::unexpected(QueryError("Vulkan video image format compatibility query", result));
  }

  [[nodiscard]] auto SelectImageFormats(Queries const &queries,
    VkPhysicalDevice device,
    VkVideoProfileListInfoKHR const &profile_list,
    VkImageUsageFlags usage,
    std::vector<VkVideoFormatPropertiesKHR> const &properties) -> std::expected<std::vector<ImageFormat>, Error>
  {
    std::vector<ImageFormat> formats;
    for (auto const &entry : properties) {
      VkImageFormatProperties2 base_image_properties{};
      base_image_properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
      auto base_usage = SupportsImageUsage(queries, device, profile_list, entry, usage, 0, &base_image_properties);
      if (!base_usage) { return std::unexpected(base_usage.error()); }
      if (!*base_usage) { continue; }
      auto transfer_destination =
        SupportsImageUsage(queries, device, profile_list, entry, usage, VK_IMAGE_USAGE_TRANSFER_DST_BIT, nullptr);
      if (!transfer_destination) { return std::unexpected(transfer_destination.error()); }
      auto storage =
        SupportsImageUsage(queries, device, profile_list, entry, usage, VK_IMAGE_USAGE_STORAGE_BIT, nullptr);
      if (!storage) { return std::unexpected(storage.error()); }
      formats.push_back(ImageFormat{ .format = entry.format,
        .image_type = entry.imageType,
        .tiling = entry.imageTiling,
        .usage = entry.imageUsageFlags,
        .create_flags = entry.imageCreateFlags,
        .components = entry.componentMapping,
        .max_extent = base_image_properties.imageFormatProperties.maxExtent,
        .transfer_destination = *transfer_destination,
        .storage = *storage });
    }
    return formats;
  }

  [[nodiscard]] auto QueryFormats(Queries const &queries,
    VkPhysicalDevice device,
    VkVideoProfileInfoKHR const &profile,
    VkImageUsageFlags usage) -> std::expected<std::vector<ImageFormat>, Error>
  {
    VkVideoProfileListInfoKHR profile_list{
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
      .pNext = nullptr,
      .profileCount = 1,
      .pProfiles = &profile,
    };
    VkPhysicalDeviceVideoFormatInfoKHR const format_info{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
      .pNext = &profile_list,
      .imageUsage = usage,
    };
    for (std::uint32_t attempt = 0; attempt < kMaximumEnumerationAttempts; ++attempt) {
      std::uint32_t count = 0;
      auto result = queries.formats(device, &format_info, &count, nullptr);
      if (result == VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR
          || result == VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR) {
        return std::vector<ImageFormat>{};
      }
      if (result != VK_SUCCESS) { return std::unexpected(QueryError("Vulkan video format enumeration", result)); }
      std::vector<VkVideoFormatPropertiesKHR> properties(count);
      for (auto &entry : properties) { entry.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR; }
      result = queries.formats(device, &format_info, &count, properties.data());
      if (result == VK_INCOMPLETE) { continue; }
      if (result != VK_SUCCESS) { return std::unexpected(QueryError("Vulkan video format enumeration", result)); }
      properties.resize(count);
      return SelectImageFormats(queries, device, profile_list, usage, properties);
    }
    return std::unexpected(QueryError("Vulkan video format enumeration remained incomplete"));
  }

}// namespace

auto Profile::preferred_source_format() const noexcept -> ImageFormat const *
{
  auto const suitable = [&](ImageFormat const &format) -> bool { return ExtentFor(*this, format).width != 0; };
  auto const nv12 = std::ranges::find_if(source_formats, [&](ImageFormat const &format) -> bool {
    return format.format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM && suitable(format);
  });
  if (nv12 != source_formats.end()) { return &*nv12; }
  auto const found = std::ranges::find_if(source_formats, suitable);
  return found == source_formats.end() ? nullptr : &*found;
}

auto Profile::preferred_conversion_format() const noexcept -> ImageFormat const *
{
  auto const suitable = [&](ImageFormat const &format) -> bool {
    return ExtentFor(*this, format).width != 0 && (format.transfer_destination || format.storage);
  };
  auto const nv12 = std::ranges::find_if(source_formats, [&](ImageFormat const &format) -> bool {
    return format.format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM && suitable(format);
  });
  if (nv12 != source_formats.end()) { return &*nv12; }
  auto const found = std::ranges::find_if(source_formats, suitable);
  return found == source_formats.end() ? nullptr : &*found;
}

auto Profile::preferred_dpb_format() const noexcept -> ImageFormat const *
{ return dpb_formats.empty() ? nullptr : &dpb_formats.front(); }

auto Profile::usable_extent() const noexcept -> VkExtent2D
{
  auto const *format = preferred_source_format();
  return format == nullptr ? VkExtent2D{} : ExtentFor(*this, *format);
}

auto Profile::supports_extent(VkExtent2D extent) const noexcept -> bool
{
  if (extent.width == 0 || extent.height == 0 || extent.width < capabilities.minCodedExtent.width
      || extent.height < capabilities.minCodedExtent.height) {
    return false;
  }
  return std::ranges::any_of(source_formats, [&](ImageFormat const &format) -> bool {
    auto const maximum = ExtentFor(*this, format);
    return extent.width <= maximum.width && extent.height <= maximum.height;
  });
}

namespace {

  struct DpbChoice
  {
    ImageFormat const *format = nullptr;
    VkExtent2D extent{};
  };

  [[nodiscard]] auto SelectSource(Profile const &profile, EncodeRequirements const &requirements) -> ImageFormat const *
  {
    if (!requirements.required_format && !requirements.require_transfer_destination
        && requirements.required_extent.width == 0 && requirements.required_extent.height == 0) {
      return requirements.require_conversion ? profile.preferred_conversion_format()
                                             : profile.preferred_source_format();
    }
    auto const found = std::ranges::find_if(profile.source_formats, [&](ImageFormat const &format) -> bool {
      auto const maximum = ExtentFor(profile, format);
      return (!requirements.required_format || format.format == *requirements.required_format)
             && (!requirements.require_conversion || format.transfer_destination || format.storage)
             && (!requirements.require_transfer_destination || format.transfer_destination)
             && maximum.width >= requirements.required_extent.width
             && maximum.height >= requirements.required_extent.height;
    });
    return found == profile.source_formats.end() ? nullptr : &*found;
  }

  [[nodiscard]] auto SelectDpb(Profile const &profile, VkExtent2D maximum) -> DpbChoice
  {
    DpbChoice selected;
    if (profile.capabilities.maxDpbSlots == 0 || profile.capabilities.maxActiveReferencePictures == 0) {
      return selected;
    }
    for (auto const &format : profile.dpb_formats) {
      auto const format_extent = ExtentFor(profile, format);
      if (format_extent.width == 0) { continue; }
      VkExtent2D const candidate_extent{
        .width = std::min(maximum.width, format_extent.width),
        .height = std::min(maximum.height, format_extent.height),
      };
      auto const candidate_score = std::tuple{
        candidate_extent.width == maximum.width && candidate_extent.height == maximum.height,
        static_cast<std::uint64_t>(candidate_extent.width) * candidate_extent.height,
        candidate_extent.width,
        candidate_extent.height,
      };
      auto const selected_score = std::tuple{
        selected.extent.width == maximum.width && selected.extent.height == maximum.height,
        static_cast<std::uint64_t>(selected.extent.width) * selected.extent.height,
        selected.extent.width,
        selected.extent.height,
      };
      if (selected.format == nullptr || candidate_score > selected_score) {
        selected = { .format = &format, .extent = candidate_extent };
      }
    }
    return selected;
  }

}// namespace

auto Report::select(EncodeRequirements requirements) const noexcept -> std::optional<EncodeSelection>
{
  if (!missing_extensions.empty() || encode_queue_families.empty()) { return std::nullopt; }
  for (std::size_t index = 0; index < profiles.size(); ++index) {
    auto const &profile = profiles.at(index);
    auto const *source = SelectSource(profile, requirements);
    if (source == nullptr) { continue; }
    auto maximum = ExtentFor(profile, *source);
    auto const dpb = SelectDpb(profile, maximum);
    if (requirements.require_references && dpb.format == nullptr) { continue; }
    bool references_at_maximum =
      dpb.format != nullptr && dpb.extent.width == maximum.width && dpb.extent.height == maximum.height;
    if (requirements.require_references) {
      maximum = dpb.extent;
      references_at_maximum = true;
    }
    std::optional<ImageFormat> selected_dpb;
    if (references_at_maximum) { selected_dpb = *dpb.format; }
    auto upload_strategy = UploadStrategy::kUnavailable;
    if (source->transfer_destination) {
      upload_strategy = UploadStrategy::kTransferDestination;
    } else if (source->storage) {
      upload_strategy = UploadStrategy::kStorageWrite;
    }
    return EncodeSelection{
      .profile_index = index,
      .profile = profile.profile,
      .source_format = *source,
      .dpb_format = selected_dpb,
      .max_extent = maximum,
      .upload_strategy = upload_strategy,
      .capture_conversion_supported = source->transfer_destination || source->storage,
      .reference_pictures_supported = references_at_maximum,
    };
  }
  return std::nullopt;
}

auto Report::selected_configuration() const noexcept -> std::optional<EncodeSelection>
{
  if (auto converted = select({ .require_conversion = true,
        .require_references = false,
        .required_format = std::nullopt,
        .require_transfer_destination = false,
        .required_extent = {} });
    converted) {
    return converted;
  }
  return select();
}

auto Report::preferred_profile() const noexcept -> Profile const *
{
  auto const selected = selected_configuration();
  return selected ? &profiles.at(selected->profile_index) : nullptr;
}

auto Report::usable() const noexcept -> bool { return select().has_value(); }

auto Report::capture_conversion_supported() const noexcept -> bool
{
  auto const selected = selected_configuration();
  return selected && selected->capture_conversion_supported;
}

auto Report::reference_pictures_supported() const noexcept -> bool
{
  auto const selected = selected_configuration();
  return selected && selected->reference_pictures_supported;
}

auto Report::diagnostic() const -> std::string
{
  std::string result = "Device: " + device_name;
  for (auto const &extension : missing_extensions) { result += "\nMissing extension: " + extension; }
  if (!usable() && !unavailable_reason.empty()) { result += "\nUnavailable: " + unavailable_reason; }
  if (auto const selected = selected_configuration(); selected) {
    auto const &profile = profiles.at(selected->profile_index);
    result += "\nEncode queue family: " + std::to_string(encode_queue_families.front());
    result += "\nH.264 profile: " + std::string{ ProfileName(selected->profile) };
    auto const extent = selected->max_extent;
    result += "\nUsable coded extent: " + std::to_string(extent.width) + "x" + std::to_string(extent.height);
    result += "\nEncode source format: " + FormatName(selected->source_format.format);
    if (selected->dpb_format) {
      result += "\nDPB format: " + FormatName(selected->dpb_format->format);
    } else {
      result += "\nDPB format: none for selected extent (all-intra configuration)";
    }
    result += "\nEncode source formats: " + std::to_string(profile.source_formats.size());
    result += "\nDPB formats: " + std::to_string(profile.dpb_formats.size());
    switch (selected->upload_strategy) {
    case UploadStrategy::kTransferDestination:
      result += "\nEncode image upload: transfer destination";
      break;
    case UploadStrategy::kStorageWrite:
      result += "\nEncode image upload: storage write";
      break;
    case UploadStrategy::kUnavailable:
      result += "\nEncode image upload: unavailable";
      break;
    }
    result +=
      selected->reference_pictures_supported ? "\nReference pictures: supported" : "\nReference pictures: unavailable";
  }
  return result;
}

namespace {

  auto DiscoverQueues(Queries const &queries, VkPhysicalDevice device, Report &report) -> void
  {
    std::uint32_t queue_count = 0;
    queries.queue_families(device, &queue_count, nullptr);
    std::vector<VkQueueFamilyProperties2> queues(queue_count);
    std::vector<VkQueueFamilyVideoPropertiesKHR> video_queues(queue_count);
    for (std::uint32_t index = 0; index < queue_count; ++index) {
      queues.at(index).sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
      queues.at(index).pNext = &video_queues.at(index);
      video_queues.at(index).sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
    }
    auto const allocated_queue_count = queue_count;
    queries.queue_families(device, &queue_count, queues.data());
    queue_count = std::min(queue_count, allocated_queue_count);
    for (std::uint32_t index = 0; index < queue_count; ++index) {
      auto const &queue = queues.at(index).queueFamilyProperties;
      auto const codec_operations = video_queues.at(index).videoCodecOperations;
      report.queue_families.push_back(
        Report::QueueFamily{ .index = index, .flags = queue.queueFlags, .codec_operations = codec_operations });
      if ((queue.queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) != 0
          && (codec_operations & VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR) != 0 && queue.queueCount != 0) {
        report.encode_queue_families.push_back(index);
      }
    }
  }

  auto ProbeProfile(Queries const &queries, VkPhysicalDevice device, StdVideoH264ProfileIdc profile_id, Report &report)
    -> std::expected<void, Error>
  {
    VkVideoEncodeH264ProfileInfoKHR h264_profile{
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
      .pNext = nullptr,
      .stdProfileIdc = profile_id,
    };
    VkVideoProfileInfoKHR const profile_info{
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &h264_profile,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
      .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
      .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
      .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    };
    VkVideoEncodeH264CapabilitiesKHR h264{};
    h264.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR;
    VkVideoEncodeCapabilitiesKHR encode{};
    encode.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR;
    encode.pNext = &h264;
    VkVideoCapabilitiesKHR capabilities{};
    capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
    capabilities.pNext = &encode;
    auto const result = queries.capabilities(device, &profile_info, &capabilities);
    if (UnsupportedProfile(result)) { return {}; }
    if (result != VK_SUCCESS) { return std::unexpected(QueryError("Vulkan H.264 capability query", result)); }
    if (std::string_view{ std::data(capabilities.stdHeaderVersion.extensionName) }
          != VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME
        || VK_VERSION_MAJOR(capabilities.stdHeaderVersion.specVersion)
             != VK_VERSION_MAJOR(VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION)) {
      report.unavailable_reason = "Incompatible H.264 Vulkan Video Std header version";
      return {};
    }
    auto source = QueryFormats(queries, device, profile_info, VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);
    if (!source) { return std::unexpected(source.error()); }
    auto dpb = QueryFormats(queries, device, profile_info, VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR);
    if (!dpb) { return std::unexpected(dpb.error()); }
    capabilities.pNext = nullptr;
    encode.pNext = nullptr;
    h264.pNext = nullptr;
    report.profiles.push_back(Profile{ .profile = profile_id,
      .capabilities = capabilities,
      .encode = encode,
      .h264 = h264,
      .source_formats = std::move(*source),
      .dpb_formats = std::move(*dpb) });
    return {};
  }

}// namespace

auto ProbeH264Capabilities(Queries const &queries, VkPhysicalDevice device) -> std::expected<Report, Error>
{
  Report report;
  VkPhysicalDeviceProperties properties{};
  queries.device_properties(device, &properties);
  report.device_name = std::string_view{ std::data(properties.deviceName) };
  report.api_version = properties.apiVersion;
  report.driver_version = properties.driverVersion;

  auto extensions = EnumerateExtensions(queries, device);
  if (!extensions) { return std::unexpected(extensions.error()); }
  for (char const *required : kRequiredExtensions) {
    if (!std::ranges::any_of(*extensions, [required](VkExtensionProperties const &extension) -> bool {
          return std::string_view{ std::data(extension.extensionName) } == required;
        })) {
      report.missing_extensions.emplace_back(required);
    }
  }
  if (!report.missing_extensions.empty()) {
    report.unavailable_reason = "Missing Vulkan Video extension: " + report.missing_extensions.front();
    return report;
  }
  if (!queries.capabilities || !queries.formats) {
    report.unavailable_reason = "Vulkan Video instance entry points are unavailable";
    return report;
  }

  DiscoverQueues(queries, device, report);
  if (report.encode_queue_families.empty()) {
    report.unavailable_reason = "No H.264 encode queue family";
    return report;
  }

  for (auto const profile_id : kProfiles) {
    auto result = ProbeProfile(queries, device, profile_id, report);
    if (!result) { return std::unexpected(result.error()); }
  }
  if (report.usable()) {
    report.unavailable_reason.clear();
  } else if (report.profiles.empty() && report.unavailable_reason.empty()) {
    report.unavailable_reason = "No supported 8-bit 4:2:0 H.264 profile";
  } else if (!report.profiles.empty() && report.preferred_profile() == nullptr) {
    report.unavailable_reason = "No profile has an encode-source format with a valid coded extent";
  }
  return report;
}

auto ProbeH264Capabilities(vkb::InstanceDispatchTable const &dispatch, VkPhysicalDevice device)
  -> std::expected<Report, Error>
{
  Queries queries;
  queries.device_properties = [&](VkPhysicalDevice physical_device, VkPhysicalDeviceProperties *properties) -> void {
    dispatch.getPhysicalDeviceProperties(physical_device, properties);
  };
  queries.extensions =
    [&](VkPhysicalDevice physical_device, std::uint32_t *count, VkExtensionProperties *properties) -> VkResult {
    return dispatch.enumerateDeviceExtensionProperties(physical_device, nullptr, count, properties);
  };
  queries.queue_families =
    [&](VkPhysicalDevice physical_device, std::uint32_t *count, VkQueueFamilyProperties2 *properties) -> void {
    dispatch.getPhysicalDeviceQueueFamilyProperties2(physical_device, count, properties);
  };
  if (dispatch.fp_vkGetPhysicalDeviceVideoCapabilitiesKHR != nullptr) {
    queries.capabilities = [&](VkPhysicalDevice physical_device,
                             VkVideoProfileInfoKHR const *profile,
                             VkVideoCapabilitiesKHR *capabilities) -> VkResult {
      return dispatch.getPhysicalDeviceVideoCapabilitiesKHR(physical_device, profile, capabilities);
    };
  }
  if (dispatch.fp_vkGetPhysicalDeviceVideoFormatPropertiesKHR != nullptr) {
    queries.formats = [&](VkPhysicalDevice physical_device,
                        VkPhysicalDeviceVideoFormatInfoKHR const *info,
                        std::uint32_t *count,
                        VkVideoFormatPropertiesKHR *properties) -> VkResult {
      return dispatch.getPhysicalDeviceVideoFormatPropertiesKHR(physical_device, info, count, properties);
    };
  }
  queries.image_format = [&](VkPhysicalDevice physical_device,
                           VkPhysicalDeviceImageFormatInfo2 const *info,
                           VkImageFormatProperties2 *properties) -> VkResult {
    return dispatch.getPhysicalDeviceImageFormatProperties2(physical_device, info, properties);
  };
  return ProbeH264Capabilities(queries, device);
}

}// namespace vkgsplat::vulkan::video
