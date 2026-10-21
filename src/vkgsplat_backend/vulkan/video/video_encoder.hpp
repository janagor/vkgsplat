#ifndef VKGSPLAT_BACKEND_VULKAN_VIDEO_VIDEO_ENCODER_HPP
#define VKGSPLAT_BACKEND_VULKAN_VIDEO_VIDEO_ENCODER_HPP

#include "video_device.hpp"
#include "video_session.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_backend/vkgsplat_backend_export.hpp>

#include <vkgsplat_video/encoded_packet.hpp>

namespace vkgsplat::vulkan::video {

struct EncoderResources;

// The producer creates an NV12 image view with VIDEO_ENCODE_SRC usage and the
// selected H.264 profile, makes the image accessible to the
// encode queue, and submits its transition to VIDEO_ENCODE_SRC_KHR before signaling
// ready_binary_semaphore. The image and semaphore remain valid until EncodeImage
// succeeds or failed in-flight work has been drained.
struct PreparedVideoFrame
{
  VkImageView image_view = VK_NULL_HANDLE;
  VkExtent2D extent{};
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkSemaphore ready_binary_semaphore = VK_NULL_HANDLE;
};

// Blocking all-intra H.264 encoder. The caller supplies a tightly packed NV12 frame.
// This API also establishes the session and packet contract for a future GPU source.
class VKGSPLAT_BACKEND_EXPORT VideoEncoder
{
public:
  [[nodiscard]] static auto Create(Context &context, EncodeDevice device, VkExtent2D extent)
    -> std::expected<std::unique_ptr<VideoEncoder>, Error>;

  [[nodiscard]] auto EncodeNv12(std::span<std::byte const> frame, std::uint64_t frame_index)
    -> std::expected<EncodedPacket, Error>;
  [[nodiscard]] auto EncodeImage(PreparedVideoFrame const &frame, std::uint64_t frame_index)
    -> std::expected<EncodedPacket, Error>;
  [[nodiscard]] auto WaitForInFlightWork() -> std::expected<void, Error>;
  auto Abort() noexcept -> void { failed_ = true; }
  [[nodiscard]] auto Finish() -> std::expected<void, Error>;
  [[nodiscard]] auto ParameterSets() const noexcept -> std::span<std::byte const>;

  ~VideoEncoder();

  VideoEncoder(VideoEncoder const &) = delete;
  auto operator=(VideoEncoder const &) -> VideoEncoder & = delete;

private:
  VideoEncoder(Context &context, EncodeDevice device, VkExtent2D extent,
    std::unique_ptr<VideoSession> session) noexcept;
  [[nodiscard]] auto EncodeSource(VkImageView source_view, VkSemaphore ready_semaphore,
    std::span<std::byte const> frame, std::uint64_t frame_index) -> std::expected<EncodedPacket, Error>;

  Context &context_;
  EncodeDevice device_;
  VkExtent2D extent_{};
  std::unique_ptr<VideoSession> session_;
  std::unique_ptr<EncoderResources> resources_;
  std::uint64_t next_index_{};
  bool finished_ = false;
  bool failed_ = false;
};

}// namespace vkgsplat::vulkan::video

#endif// VKGSPLAT_BACKEND_VULKAN_VIDEO_VIDEO_ENCODER_HPP
