#include <catch2/catch_test_macros.hpp>

#include "vulkan/video/video_capabilities.hpp"
#include "vulkan/video/video_device.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace video = vkgsplat::vulkan::video;

namespace {

constexpr std::uint32_t kMinimumExtent = 64U;
constexpr std::uint32_t kMaximumExtent = 1920U;
constexpr std::uint32_t kAlternativeMaximumExtent = 3840U;
constexpr std::uint32_t kSmallerDpbExtent = 1280U;
constexpr std::uint32_t kTooSmallExtent = 32U;
constexpr std::uint32_t kEncodeQueueFamily = 0U;

struct ProbeFixture
{
  std::vector<std::string> extensions{
    VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
    VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
  };
  bool encode_queue = true;
  bool main_profile = true;
  bool baseline_profile = false;
  bool high_profile = false;
  bool main_header_compatible = true;
  bool source_format = true;
  bool alternative_source_format = false;
  bool alternative_dpb_format = false;
  bool dpb_format = false;
  bool main_dpb_format = true;
  bool baseline_dpb_format = true;
  bool high_dpb_format = true;
  std::uint32_t max_dpb_slots = 0;
  std::uint32_t max_active_references = 0;
  bool transfer_destination = true;
  bool main_transfer_destination = true;
  bool baseline_transfer_destination = true;
  bool high_transfer_destination = true;
  bool nv12_transfer_destination = true;
  bool alternative_transfer_destination = true;
  bool storage = false;
  bool supports_disjoint_images = false;
  bool queried_optional_create_flags = false;
  bool incomplete_extensions = false;
  bool incomplete_formats = false;
  bool missing_video_entry_point = false;
  VkResult capability_error = VK_SUCCESS;
  VkResult extra_usage_error = VK_SUCCESS;
  std::uint32_t format_max_extent = kMaximumExtent;
  std::uint32_t codec_max_extent = kMaximumExtent;
  std::uint32_t nv12_max_extent = kMaximumExtent;
  std::uint32_t alternative_max_extent = kMaximumExtent;
  std::uint32_t extension_fills = 0;
  std::uint32_t format_fills = 0;
};

auto MakeQueries(ProbeFixture &fixture) -> video::Queries
{
  video::Queries queries;
  queries.device_properties = [](VkPhysicalDevice, VkPhysicalDeviceProperties *properties) -> void {
    std::strcpy(properties->deviceName, "test device");
  };
  queries.extensions = [&](VkPhysicalDevice, std::uint32_t *count, VkExtensionProperties *properties) -> VkResult {
    *count = static_cast<std::uint32_t>(fixture.extensions.size());
    if (properties == nullptr) { return VK_SUCCESS; }
    ++fixture.extension_fills;
    if (fixture.incomplete_extensions && fixture.extension_fills == 1U) { return VK_INCOMPLETE; }
    for (std::size_t index = 0; index < fixture.extensions.size(); ++index) {
      std::strcpy(properties[index].extensionName, fixture.extensions[index].c_str());
    }
    return VK_SUCCESS;
  };
  queries.queue_families = [&](VkPhysicalDevice,
                             std::uint32_t *count,
                             VkQueueFamilyProperties2 *properties) -> void {
    *count = 1U;
    if (properties == nullptr) { return; }
    properties[kEncodeQueueFamily].queueFamilyProperties.queueCount = 1U;
    auto *video_properties = static_cast<VkQueueFamilyVideoPropertiesKHR *>(properties[kEncodeQueueFamily].pNext);
    if (fixture.encode_queue) {
      properties[kEncodeQueueFamily].queueFamilyProperties.queueFlags = VK_QUEUE_VIDEO_ENCODE_BIT_KHR;
      video_properties->videoCodecOperations = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
    }
  };
  if (!fixture.missing_video_entry_point) {
    queries.capabilities = [&](VkPhysicalDevice,
                             VkVideoProfileInfoKHR const *profile,
                             VkVideoCapabilitiesKHR *capabilities) -> VkResult {
      if (fixture.capability_error != VK_SUCCESS) { return fixture.capability_error; }
      auto const *h264_profile = static_cast<VkVideoEncodeH264ProfileInfoKHR const *>(profile->pNext);
      if ((h264_profile->stdProfileIdc == STD_VIDEO_H264_PROFILE_IDC_MAIN && !fixture.main_profile)
          || (h264_profile->stdProfileIdc == STD_VIDEO_H264_PROFILE_IDC_BASELINE && !fixture.baseline_profile)
          || (h264_profile->stdProfileIdc == STD_VIDEO_H264_PROFILE_IDC_HIGH && !fixture.high_profile)) {
        return VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR;
      }
      capabilities->minCodedExtent = { kMinimumExtent, kMinimumExtent };
      capabilities->maxCodedExtent = { fixture.codec_max_extent, fixture.codec_max_extent };
      capabilities->maxDpbSlots = fixture.max_dpb_slots;
      capabilities->maxActiveReferencePictures = fixture.max_active_references;
      std::strcpy(capabilities->stdHeaderVersion.extensionName,
        h264_profile->stdProfileIdc == STD_VIDEO_H264_PROFILE_IDC_MAIN && !fixture.main_header_compatible
          ? "unsupported header"
          : VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME);
      capabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION;
      return VK_SUCCESS;
    };
    queries.formats = [&](VkPhysicalDevice,
                        VkPhysicalDeviceVideoFormatInfoKHR const *info,
                        std::uint32_t *count,
                        VkVideoFormatPropertiesKHR *properties) -> VkResult {
      bool const source = info->imageUsage == VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR;
      auto const *profile_list = static_cast<VkVideoProfileListInfoKHR const *>(info->pNext);
      auto const *h264_profile = static_cast<VkVideoEncodeH264ProfileInfoKHR const *>(profile_list->pProfiles->pNext);
      bool const main = h264_profile->stdProfileIdc == STD_VIDEO_H264_PROFILE_IDC_MAIN;
      bool const baseline = h264_profile->stdProfileIdc == STD_VIDEO_H264_PROFILE_IDC_BASELINE;
      bool dpb_available = fixture.high_dpb_format;
      if (main) { dpb_available = fixture.main_dpb_format; }
      else if (baseline) { dpb_available = fixture.baseline_dpb_format; }
      bool const available = source ? fixture.source_format : fixture.dpb_format && dpb_available;
      bool const second_format = source ? fixture.alternative_source_format : fixture.alternative_dpb_format;
      *count = available ? (second_format ? 2U : 1U) : 0U;
      if (properties == nullptr || !available) { return VK_SUCCESS; }
      ++fixture.format_fills;
      if (fixture.incomplete_formats && fixture.format_fills == 1U) { return VK_INCOMPLETE; }
      bool profile_transfer = fixture.high_transfer_destination;
      if (main) { profile_transfer = fixture.main_transfer_destination; }
      else if (baseline) { profile_transfer = fixture.baseline_transfer_destination; }
      properties[0].format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
      properties[0].imageType = VK_IMAGE_TYPE_2D;
      properties[0].imageTiling = VK_IMAGE_TILING_OPTIMAL;
      properties[0].imageCreateFlags = fixture.supports_disjoint_images
                                       ? VK_IMAGE_CREATE_DISJOINT_BIT : 0;
      properties[0].imageUsageFlags = info->imageUsage;
      if (fixture.transfer_destination && profile_transfer && fixture.nv12_transfer_destination) {
        properties[0].imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      }
      if (fixture.storage) { properties[0].imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT; }
      if (second_format) {
        properties[1] = properties[0];
        properties[1].format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
        properties[1].imageUsageFlags = info->imageUsage;
        if (fixture.transfer_destination && profile_transfer && fixture.alternative_transfer_destination) {
          properties[1].imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        if (fixture.storage) { properties[1].imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT; }
      }
      return VK_SUCCESS;
    };
  }
  queries.image_format = [&](VkPhysicalDevice,
                           VkPhysicalDeviceImageFormatInfo2 const *info,
                           VkImageFormatProperties2 *properties) -> VkResult {
    if (info->flags != 0) { fixture.queried_optional_create_flags = true; }
    if (fixture.supports_disjoint_images && info->flags != 0) {
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    bool const extra_usage = (info->usage & (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0;
    if (extra_usage && fixture.extra_usage_error != VK_SUCCESS) { return fixture.extra_usage_error; }
    auto const format_maximum = info->format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM
                                  ? fixture.alternative_max_extent
                                  : fixture.nv12_max_extent;
    auto const maximum = std::min(fixture.format_max_extent, format_maximum);
    properties->imageFormatProperties.maxExtent = { maximum, maximum, 1U };
    return VK_SUCCESS;
  };
  return queries;
}

auto Probe(ProbeFixture &fixture) -> std::expected<video::Report, vkgsplat::Error>
{
  auto queries = MakeQueries(fixture);
  return video::ProbeH264Capabilities(queries, VK_NULL_HANDLE);
}

}// namespace

TEST_CASE("Optional video image creation flags are not required by capability queries")
{
  ProbeFixture fixture;
  fixture.supports_disjoint_images = true;
  auto report = Probe(fixture);
  REQUIRE(report);
  CHECK_FALSE(fixture.queried_optional_create_flags);
  REQUIRE(report->selected_configuration());
  CHECK(report->selected_configuration()->source_format.transfer_destination);
}

TEST_CASE("Explicit NV12 upload selection searches later profiles")
{
  ProbeFixture fixture;
  fixture.baseline_profile = true;
  fixture.main_transfer_destination = false;
  auto report = Probe(fixture);
  REQUIRE(report);
  auto selection = report->select({
    .require_conversion = true,
    .require_references = false,
    .required_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
    .require_transfer_destination = true,
    .required_extent = { kMinimumExtent, kMinimumExtent },
  });
  REQUIRE(selection);
  CHECK(selection->profile == STD_VIDEO_H264_PROFILE_IDC_BASELINE);
  CHECK(selection->source_format.transfer_destination);
}

TEST_CASE("Probe reports missing extensions and an unavailable encode queue")
{
  ProbeFixture fixture;
  fixture.extensions.pop_back();
  auto missing_extension = Probe(fixture);
  REQUIRE(missing_extension);
  CHECK_FALSE(missing_extension->usable());
  CHECK(missing_extension->missing_extensions == std::vector<std::string>{ VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME });

  fixture.extensions.emplace_back(VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME);
  fixture.encode_queue = false;
  auto missing_queue = Probe(fixture);
  REQUIRE(missing_queue);
  CHECK_FALSE(missing_queue->usable());
  CHECK(missing_queue->unavailable_reason == "No H.264 encode queue family");
}

TEST_CASE("Probe handles incomplete enumeration and unsupported profiles")
{
  ProbeFixture fixture;
  fixture.incomplete_extensions = true;
  fixture.incomplete_formats = true;
  fixture.main_profile = false;
  fixture.baseline_profile = true;
  auto report = Probe(fixture);
  REQUIRE(report);
  REQUIRE(report->usable());
  CHECK(report->preferred_profile()->profile == STD_VIDEO_H264_PROFILE_IDC_BASELINE);
  CHECK(fixture.extension_fills > 1U);
  CHECK(fixture.format_fills > 1U);
}

TEST_CASE("Probe separates H.264 support from conversion and reference pictures")
{
  ProbeFixture fixture;
  fixture.transfer_destination = false;
  auto report = Probe(fixture);
  REQUIRE(report);
  CHECK(report->usable());
  CHECK_FALSE(report->capture_conversion_supported());
  CHECK_FALSE(report->reference_pictures_supported());
  CHECK(video::ChooseEncodeQueue(*report) == kEncodeQueueFamily);
  CHECK(report->preferred_profile()->supports_extent({ kMinimumExtent, kMinimumExtent }));
  CHECK_FALSE(report->preferred_profile()->supports_extent({ kTooSmallExtent, kTooSmallExtent }));
}

TEST_CASE("Probe propagates unexpected query errors and tolerates unsupported extra usage")
{
  ProbeFixture fixture;
  fixture.capability_error = VK_ERROR_OUT_OF_HOST_MEMORY;
  CHECK_FALSE(Probe(fixture));

  fixture.capability_error = VK_SUCCESS;
  fixture.extra_usage_error = VK_ERROR_FORMAT_NOT_SUPPORTED;
  auto report = Probe(fixture);
  REQUIRE(report);
  CHECK(report->usable());
  CHECK_FALSE(report->capture_conversion_supported());

  fixture.extra_usage_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
  CHECK_FALSE(Probe(fixture));
}

TEST_CASE("Probe treats absent video entry points as unavailable")
{
  ProbeFixture fixture;
  fixture.missing_video_entry_point = true;
  auto report = Probe(fixture);
  REQUIRE(report);
  CHECK_FALSE(report->usable());
  CHECK(report->unavailable_reason == "Vulkan Video instance entry points are unavailable");
}

TEST_CASE("Probe handles multiple profiles and an empty coded extent intersection")
{
  ProbeFixture fixture;
  fixture.baseline_profile = true;
  auto multiple = Probe(fixture);
  REQUIRE(multiple);
  REQUIRE(multiple->profiles.size() == 2U);
  CHECK(multiple->preferred_profile()->profile == STD_VIDEO_H264_PROFILE_IDC_MAIN);

  fixture.format_max_extent = kTooSmallExtent;
  auto invalid_extent = Probe(fixture);
  REQUIRE(invalid_extent);
  CHECK_FALSE(invalid_extent->usable());
  CHECK(invalid_extent->preferred_profile() == nullptr);
}

TEST_CASE("Reference pictures require both DPB limits and a compatible format")
{
  ProbeFixture fixture;
  fixture.max_dpb_slots = 1U;
  fixture.max_active_references = 1U;
  auto missing_dpb = Probe(fixture);
  REQUIRE(missing_dpb);
  CHECK(missing_dpb->usable());
  CHECK_FALSE(missing_dpb->reference_pictures_supported());

  fixture.dpb_format = true;
  auto with_dpb = Probe(fixture);
  REQUIRE(with_dpb);
  CHECK(with_dpb->reference_pictures_supported());
}

TEST_CASE("A later supported profile clears earlier profile diagnostics")
{
  ProbeFixture fixture;
  fixture.main_header_compatible = false;
  fixture.baseline_profile = true;
  auto report = Probe(fixture);
  REQUIRE(report);
  CHECK(report->usable());
  CHECK(report->unavailable_reason.empty());
  CHECK(report->preferred_profile()->profile == STD_VIDEO_H264_PROFILE_IDC_BASELINE);
}

TEST_CASE("Required selection retains the renderer's preferred GPU and ranks fallbacks")
{
  std::array candidates{
    video::CandidateInfo{ .device_type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, .name = "z" },
    video::CandidateInfo{ .preferred_rendering_device = true,
      .device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
      .name = "preferred" },
    video::CandidateInfo{ .device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, .name = "b" },
    video::CandidateInfo{ .device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, .name = "a" },
  };
  CHECK((video::RankCandidates(candidates) == std::vector<std::size_t>{ 1U, 3U, 2U, 0U }));
}

TEST_CASE("Selected profile keeps conversion and reference flags with its own formats")
{
  ProbeFixture fixture;
  fixture.baseline_profile = true;
  fixture.main_transfer_destination = false;
  fixture.dpb_format = true;
  fixture.main_dpb_format = false;
  fixture.max_dpb_slots = 1U;
  fixture.max_active_references = 1U;
  auto report = Probe(fixture);
  REQUIRE(report);
  REQUIRE(report->usable());
  auto selected = report->selected_configuration();
  REQUIRE(selected);
  CHECK(selected->profile == STD_VIDEO_H264_PROFILE_IDC_BASELINE);
  CHECK(selected->capture_conversion_supported);
  CHECK(selected->reference_pictures_supported);
  auto main = report->select();
  REQUIRE(main);
  CHECK(main->profile == STD_VIDEO_H264_PROFILE_IDC_MAIN);
  CHECK_FALSE(main->capture_conversion_supported);
  CHECK_FALSE(main->reference_pictures_supported);
  auto references = report->select({ .require_conversion = false, .require_references = true,
    .required_format = std::nullopt, .require_transfer_destination = false, .required_extent = {} });
  REQUIRE(references);
  CHECK(references->profile == STD_VIDEO_H264_PROFILE_IDC_BASELINE);
}

TEST_CASE("Selection uses an alternative format when NV12 cannot accept conversion")
{
  ProbeFixture fixture;
  fixture.alternative_source_format = true;
  fixture.nv12_transfer_destination = false;
  auto report = Probe(fixture);
  REQUIRE(report);
  auto selected = report->selected_configuration();
  REQUIRE(selected);
  CHECK(selected->source_format.format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
  CHECK(selected->capture_conversion_supported);
  CHECK(selected->upload_strategy == video::UploadStrategy::kTransferDestination);
  auto codec_only = report->select();
  REQUIRE(codec_only);
  CHECK(codec_only->source_format.format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
}

TEST_CASE("Advertised maximum belongs to the selected format")
{
  ProbeFixture fixture;
  fixture.alternative_source_format = true;
  fixture.format_max_extent = kAlternativeMaximumExtent;
  fixture.codec_max_extent = kAlternativeMaximumExtent;
  fixture.alternative_max_extent = kAlternativeMaximumExtent;
  auto report = Probe(fixture);
  REQUIRE(report);
  auto selected = report->selected_configuration();
  REQUIRE(selected);
  CHECK(selected->source_format.format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
  CHECK(selected->max_extent.width == kMaximumExtent);
  CHECK(report->preferred_profile()->supports_extent({ kAlternativeMaximumExtent, kAlternativeMaximumExtent }));
}

TEST_CASE("No encode-source format is an unsupported capability outcome")
{
  ProbeFixture fixture;
  fixture.source_format = false;
  auto report = Probe(fixture);
  REQUIRE(report);
  CHECK_FALSE(report->usable());
  CHECK_FALSE(report->selected_configuration().has_value());
  CHECK(report->unavailable_reason == "No profile has an encode-source format with a valid coded extent");
}

TEST_CASE("Reference selection prefers the DPB format covering the source extent")
{
  ProbeFixture fixture;
  fixture.dpb_format = true;
  fixture.alternative_dpb_format = true;
  fixture.alternative_source_format = true;
  fixture.nv12_transfer_destination = false;
  fixture.max_dpb_slots = 1U;
  fixture.max_active_references = 1U;
  fixture.format_max_extent = kAlternativeMaximumExtent;
  fixture.codec_max_extent = kAlternativeMaximumExtent;
  fixture.nv12_max_extent = kSmallerDpbExtent;
  fixture.alternative_max_extent = kAlternativeMaximumExtent;
  auto report = Probe(fixture);
  REQUIRE(report);
  auto selected = report->select({ .require_conversion = true, .require_references = true,
    .required_format = std::nullopt, .require_transfer_destination = false, .required_extent = {} });
  REQUIRE(selected);
  REQUIRE(selected->dpb_format);
  CHECK(selected->source_format.format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
  CHECK(selected->dpb_format->format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
  CHECK(selected->max_extent.width == kAlternativeMaximumExtent);
}

TEST_CASE("Combined requirements select one profile satisfying both")
{
  ProbeFixture fixture;
  fixture.baseline_profile = true;
  fixture.high_profile = true;
  fixture.dpb_format = true;
  fixture.main_dpb_format = false;
  fixture.baseline_transfer_destination = false;
  fixture.max_dpb_slots = 1U;
  fixture.max_active_references = 1U;
  auto report = Probe(fixture);
  REQUIRE(report);
  auto selected = report->select({ .require_conversion = true, .require_references = true,
    .required_format = std::nullopt, .require_transfer_destination = false, .required_extent = {} });
  REQUIRE(selected);
  REQUIRE(selected->dpb_format);
  CHECK(selected->profile == STD_VIDEO_H264_PROFILE_IDC_HIGH);
  CHECK(selected->capture_conversion_supported);
  CHECK(selected->reference_pictures_supported);
  CHECK(selected->source_format.format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
  CHECK(selected->dpb_format->format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
}
