#ifndef VKGSPLAT_BACKEND_VULKAN_VIDEO_CAPABILITIES_HPP
#define VKGSPLAT_BACKEND_VULKAN_VIDEO_CAPABILITIES_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan::video {

/** Narrow Vulkan query surface used by the physical-device probe. */
struct Queries
{
  std::function<void(VkPhysicalDevice, VkPhysicalDeviceProperties *)> device_properties;
  std::function<VkResult(VkPhysicalDevice, std::uint32_t *, VkExtensionProperties *)> extensions;
  std::function<void(VkPhysicalDevice, std::uint32_t *, VkQueueFamilyProperties2 *)> queue_families;
  std::function<VkResult(VkPhysicalDevice, VkVideoProfileInfoKHR const *, VkVideoCapabilitiesKHR *)> capabilities;
  std::function<VkResult(VkPhysicalDevice,
    VkPhysicalDeviceVideoFormatInfoKHR const *,
    std::uint32_t *,
    VkVideoFormatPropertiesKHR *)>
    formats;
  std::function<VkResult(VkPhysicalDevice, VkPhysicalDeviceImageFormatInfo2 const *, VkImageFormatProperties2 *)>
    image_format;
};

struct ImageFormat
{
  VkFormat format{};
  VkImageType image_type{};
  VkImageTiling tiling{};
  VkImageUsageFlags usage{};
  VkImageCreateFlags create_flags{};
  VkComponentMapping components{};
  VkExtent3D max_extent{};
  bool transfer_destination = false;
  bool storage = false;
};

struct Profile
{
  StdVideoH264ProfileIdc profile = STD_VIDEO_H264_PROFILE_IDC_MAIN;
  VkVideoCapabilitiesKHR capabilities{};
  VkVideoEncodeCapabilitiesKHR encode{};
  VkVideoEncodeH264CapabilitiesKHR h264{};
  std::vector<ImageFormat> source_formats;
  std::vector<ImageFormat> dpb_formats;

  [[nodiscard]] auto preferred_source_format() const noexcept -> ImageFormat const *;
  [[nodiscard]] auto preferred_conversion_format() const noexcept -> ImageFormat const *;
  [[nodiscard]] auto preferred_dpb_format() const noexcept -> ImageFormat const *;
  [[nodiscard]] auto usable_extent() const noexcept -> VkExtent2D;
  [[nodiscard]] auto supports_extent(VkExtent2D extent) const noexcept -> bool;
};

struct EncodeRequirements
{
  bool require_conversion = false;
  bool require_references = false;
  std::optional<VkFormat> required_format;
  bool require_transfer_destination = false;
  VkExtent2D required_extent{};
};

enum class UploadStrategy : std::uint8_t { kUnavailable, kTransferDestination, kStorageWrite };

/** Self-contained profile and image choice for a single encoder configuration. */
struct EncodeSelection
{
  std::size_t profile_index = 0;
  StdVideoH264ProfileIdc profile = STD_VIDEO_H264_PROFILE_IDC_MAIN;
  ImageFormat source_format;
  std::optional<ImageFormat> dpb_format;
  VkExtent2D max_extent{};
  UploadStrategy upload_strategy = UploadStrategy::kUnavailable;
  bool capture_conversion_supported = false;
  bool reference_pictures_supported = false;
};

struct Report
{
  struct QueueFamily
  {
    std::uint32_t index = 0;
    VkQueueFlags flags = 0;
    VkVideoCodecOperationFlagsKHR codec_operations = 0;
  };

  std::string device_name;
  std::uint32_t api_version = 0;
  std::uint32_t driver_version = 0;
  std::vector<std::string> missing_extensions;
  std::vector<std::uint32_t> encode_queue_families;
  std::vector<QueueFamily> queue_families;
  std::vector<Profile> profiles;
  std::string unavailable_reason;

  [[nodiscard]] auto usable() const noexcept -> bool;
  [[nodiscard]] auto select(EncodeRequirements requirements = {}) const noexcept -> std::optional<EncodeSelection>;
  [[nodiscard]] auto selected_configuration() const noexcept -> std::optional<EncodeSelection>;
  [[nodiscard]] auto capture_conversion_supported() const noexcept -> bool;
  [[nodiscard]] auto reference_pictures_supported() const noexcept -> bool;
  [[nodiscard]] auto preferred_profile() const noexcept -> Profile const *;
  [[nodiscard]] auto diagnostic() const -> std::string;
};

[[nodiscard]] auto ProbeH264Capabilities(vkb::InstanceDispatchTable const &dispatch, VkPhysicalDevice device)
  -> std::expected<Report, Error>;
[[nodiscard]] auto ProbeH264Capabilities(Queries const &queries, VkPhysicalDevice device)
  -> std::expected<Report, Error>;

}// namespace vkgsplat::vulkan::video

#endif// VKGSPLAT_BACKEND_VULKAN_VIDEO_CAPABILITIES_HPP
