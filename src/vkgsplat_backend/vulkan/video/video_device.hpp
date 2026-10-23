#ifndef VKGSPLAT_BACKEND_VULKAN_VIDEO_DEVICE_HPP
#define VKGSPLAT_BACKEND_VULKAN_VIDEO_DEVICE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "video_capabilities.hpp"

namespace vkgsplat::vulkan::video {

struct EncodeDevice
{
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t queue_family_index = 0;
  EncodeSelection selection;
  Report capabilities;
};

struct CandidateInfo
{
  bool preferred_rendering_device = false;
  VkPhysicalDeviceType device_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
  std::string_view name;
};

[[nodiscard]] auto ChooseEncodeQueue(Report const &report) noexcept -> std::optional<std::uint32_t>;
[[nodiscard]] auto RankCandidates(std::span<CandidateInfo const> candidates) -> std::vector<std::size_t>;

}// namespace vkgsplat::vulkan::video

#endif// VKGSPLAT_BACKEND_VULKAN_VIDEO_DEVICE_HPP
