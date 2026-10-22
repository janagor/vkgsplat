#ifndef VKGSPLAT_BACKEND_VULKAN_VIDEO_VIDEO_ENCODER_HPP
#define VKGSPLAT_BACKEND_VULKAN_VIDEO_VIDEO_ENCODER_HPP

#include "video_device.hpp"
#include "video_session.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_backend/vkgsplat_backend_export.hpp>

#include <vkgsplat_video/encoded_packet.hpp>

namespace vkgsplat::vulkan::video {

struct EncoderResources;

// Simulates rejection at the selected submission boundary in GPU tests.
enum class EncodeFailurePointForTest : std::uint8_t {
  kNone,
  kVideoSubmission,
  kReadbackSubmission,
};

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

struct PictureMetadata
{
  std::uint64_t encode_index{};
  std::int64_t presentation_timestamp{};
  std::int64_t duration = 1;
};

// All-intra H.264 encoder. A single owner submits and completes GPU pictures
// in encode order; the synchronous methods preserve offline export behavior.
class VKGSPLAT_BACKEND_EXPORT VideoEncoder
{
public:
  static constexpr std::size_t kOfflineInFlightFrames = 1U;
  static constexpr std::size_t kMaximumInFlightFrames = 8U;
  [[nodiscard]] static auto Create(Context &context, EncodeDevice device, VkExtent2D extent,
    std::uint32_t fps_numerator, std::uint32_t fps_denominator,
    std::size_t max_in_flight_frames = kOfflineInFlightFrames,
    bool fixed_frame_rate = true)
    -> std::expected<std::unique_ptr<VideoEncoder>, Error>;

  [[nodiscard]] auto EncodeNv12(std::span<std::byte const> frame, std::uint64_t frame_index)
    -> std::expected<EncodedPacket, Error>;
  [[nodiscard]] auto EncodeImage(PreparedVideoFrame const &frame, std::uint64_t frame_index)
    -> std::expected<EncodedPacket, Error>;
  // One thread owns this encoder. The source image must remain alive until
  // the matching packet is completed or failed in-flight work is drained.
  [[nodiscard]] auto SubmitImage(PreparedVideoFrame const &frame, PictureMetadata metadata,
    std::shared_ptr<void> source_owner)
    -> std::expected<void, Error>;
  [[nodiscard]] auto CompleteImage() -> std::expected<EncodedPacket, Error>;
  [[nodiscard]] auto TryCompleteImage() -> std::expected<std::optional<EncodedPacket>, Error>;
  [[nodiscard]] auto WaitForInFlightWork() -> std::expected<void, Error>;
  auto Abort() noexcept -> void { failed_ = true; }
  auto FailSubmissionForTest(EncodeFailurePointForTest point) noexcept -> void { failure_point_for_test_ = point; }
  [[nodiscard]] auto Finish() -> std::expected<void, Error>;
  [[nodiscard]] auto ParameterSets() const noexcept -> std::span<std::byte const>;

  ~VideoEncoder();

  VideoEncoder(VideoEncoder const &) = delete;
  auto operator=(VideoEncoder const &) -> VideoEncoder & = delete;
  VideoEncoder(VideoEncoder &&) = delete;
  auto operator=(VideoEncoder &&) -> VideoEncoder & = delete;

private:
  VideoEncoder(Context &context, EncodeDevice device, VkExtent2D extent,
    std::unique_ptr<VideoSession> session) noexcept;
  [[nodiscard]] auto EncodeSource(VkImageView source_view, VkSemaphore ready_semaphore,
    std::span<std::byte const> frame, std::uint64_t frame_index) -> std::expected<EncodedPacket, Error>;
  [[nodiscard]] auto CompleteReadyImage() -> std::expected<EncodedPacket, Error>;
  [[nodiscard]] auto SubmitImageImpl(PreparedVideoFrame const &frame, PictureMetadata metadata,
    std::shared_ptr<void> source_owner, bool require_owner) -> std::expected<void, Error>;

  Context &context_;
  EncodeDevice device_;
  VkExtent2D extent_{};
  std::unique_ptr<VideoSession> session_;
  std::vector<std::shared_ptr<void>> source_owners_;
  std::vector<std::unique_ptr<EncoderResources>> resources_;
  std::vector<std::optional<PictureMetadata>> pending_metadata_;
  std::uint64_t next_index_{};
  std::uint64_t next_submit_index_{};
  std::int64_t next_minimum_timestamp_{};
  bool finished_ = false;
  bool failed_ = false;
  EncodeFailurePointForTest failure_point_for_test_ = EncodeFailurePointForTest::kNone;
};

}// namespace vkgsplat::vulkan::video

#endif// VKGSPLAT_BACKEND_VULKAN_VIDEO_VIDEO_ENCODER_HPP
