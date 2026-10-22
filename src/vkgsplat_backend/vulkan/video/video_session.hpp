#ifndef VKGSPLAT_BACKEND_VULKAN_VIDEO_SESSION_HPP
#define VKGSPLAT_BACKEND_VULKAN_VIDEO_SESSION_HPP

#include "video_device.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan {
struct Context;
}

namespace vkgsplat::vulkan::video {

// Owns every allocation bound to a single all-intra video session.
class VideoSession
{
public:
  [[nodiscard]] static auto Create(Context &context, EncodeDevice const &device, VkExtent2D extent,
    std::uint32_t fps_numerator, std::uint32_t fps_denominator, bool fixed_frame_rate = true)
    -> std::expected<std::unique_ptr<VideoSession>, Error>;

  ~VideoSession();
  VideoSession(VideoSession const &) = delete;
  auto operator=(VideoSession const &) -> VideoSession & = delete;
  VideoSession(VideoSession &&) = delete;
  auto operator=(VideoSession &&) -> VideoSession & = delete;

  [[nodiscard]] auto handle() const noexcept -> VkVideoSessionKHR { return session_; }
  [[nodiscard]] auto parameters() const noexcept -> VkVideoSessionParametersKHR { return parameters_; }
  [[nodiscard]] auto parameter_sets() const noexcept -> std::vector<std::byte> const & { return parameter_sets_; }

private:
  explicit VideoSession(Context &context) noexcept : context_{ context } {}

  [[nodiscard]] static auto CreateHandle(VideoSession &owned, EncodeDevice const &device, VkExtent2D extent)
    -> std::expected<void, Error>;
  [[nodiscard]] static auto BindMemory(VideoSession &owned) -> std::expected<void, Error>;
  [[nodiscard]] static auto RetrieveParameterSets(VideoSession &owned) -> std::expected<void, Error>;

  Context &context_;
  VkVideoSessionKHR session_ = VK_NULL_HANDLE;
  VkVideoSessionParametersKHR parameters_ = VK_NULL_HANDLE;
  std::vector<VkDeviceMemory> memory_;
  std::vector<std::byte> parameter_sets_;
};

}// namespace vkgsplat::vulkan::video

#endif// VKGSPLAT_BACKEND_VULKAN_VIDEO_SESSION_HPP
