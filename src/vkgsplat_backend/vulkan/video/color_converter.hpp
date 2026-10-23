#ifndef VKGSPLAT_BACKEND_VULKAN_VIDEO_COLOR_CONVERTER_HPP
#define VKGSPLAT_BACKEND_VULKAN_VIDEO_COLOR_CONVERTER_HPP

#include "video_encoder.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <vkgsplat_backend/vkgsplat_backend_export.hpp>

namespace vkgsplat::vulkan::video {

// Converts an R8G8B8A8_UNORM image of Extent() to BT.709 limited-range NV12.
// The source must have transfer-source usage, be accessible to the graphics
// queue, and be in TRANSFER_SRC_OPTIMAL when the timeline value is signaled.
// Submit Convert on the render worker that owns the graphics queue. Keep the
// source and returned frame alive until EncodeImage succeeds. On failure, drain
// the encoder and call WaitForSourceRelease before reusing the source image.
// Abort retires the converter because the binary semaphore may be unconsumed.
// Conversion leaves the source image in TRANSFER_SRC_OPTIMAL.
class VKGSPLAT_BACKEND_EXPORT GpuColorConverter
{
public:
  [[nodiscard]] static auto Create(Context &context, EncodeDevice const &device, VkExtent2D extent)
    -> std::expected<std::unique_ptr<GpuColorConverter>, Error>;

  [[nodiscard]] auto Convert(VkImage source, VkSemaphore ready_timeline, std::uint64_t ready_value)
    -> std::expected<PreparedVideoFrame, Error>;
  [[nodiscard]] auto Extent() const noexcept -> VkExtent2D { return extent_; }
  // Internal test hook; valid after WaitForSourceRelease and before the next Convert.
  [[nodiscard]] auto Nv12BufferForTest() const noexcept -> VkBuffer { return nv12_buffer_; }
  [[nodiscard]] auto WaitForSourceRelease() -> std::expected<void, Error>;
  // Call only after EncodeImage has successfully consumed the returned frame.
  [[nodiscard]] auto Complete() -> std::expected<void, Error>;
  auto Abort() noexcept -> void { failed_ = true; }
  // Simulates a rejected queue submission after the conversion fence is reset.
  auto FailNextSubmissionForTest() noexcept -> void { fail_next_submission_for_test_ = true; }

  ~GpuColorConverter();
  GpuColorConverter(GpuColorConverter const &) = delete;
  auto operator=(GpuColorConverter const &) -> GpuColorConverter & = delete;
  GpuColorConverter(GpuColorConverter &&) = delete;
  auto operator=(GpuColorConverter &&) -> GpuColorConverter & = delete;

private:
  GpuColorConverter(Context &context, VkExtent2D extent) noexcept;

  Context &context_;
  VkExtent2D extent_{};
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkImage output_ = VK_NULL_HANDLE;
  VkImageView output_view_ = VK_NULL_HANDLE;
  VkDeviceMemory output_memory_ = VK_NULL_HANDLE;
  VkBuffer rgba_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory rgba_memory_ = VK_NULL_HANDLE;
  VkBuffer nv12_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory nv12_memory_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  VkSemaphore converted_ = VK_NULL_HANDLE;
  VkFence converted_done_ = VK_NULL_HANDLE;
  bool in_flight_ = false;
  bool failed_ = false;
  bool fail_next_submission_for_test_ = false;
};

class GpuColorConverterPool;

class VKGSPLAT_BACKEND_EXPORT ConversionLease final
{
public:
  ~ConversionLease() noexcept;
  ConversionLease(ConversionLease const &) = delete;
  auto operator=(ConversionLease const &) -> ConversionLease & = delete;
  ConversionLease(ConversionLease &&) = delete;
  auto operator=(ConversionLease &&) -> ConversionLease & = delete;

  // Call only after the encoder has accepted the prepared frame. An
  // unaccepted signal requires replacement of this converter's semaphore.
  auto MarkEncodeSubmitted() noexcept -> void { encode_submitted_ = true; }

private:
  friend class GpuColorConverterPool;
  ConversionLease(std::shared_ptr<GpuColorConverterPool> pool, std::size_t slot) noexcept;

  std::shared_ptr<GpuColorConverterPool> pool_;
  std::size_t slot_{};
  bool encode_submitted_ = false;
};

struct ConvertedFrame
{
  PreparedVideoFrame prepared;
  std::shared_ptr<ConversionLease> lease;
};

// Each lease reserves one independent converter. The pool retains its RGBA
// source until conversion has completed; the encoder retains the lease through
// readback, after which the recording owner retires released slots.
class VKGSPLAT_BACKEND_EXPORT GpuColorConverterPool final : public std::enable_shared_from_this<GpuColorConverterPool>
{
public:
  [[nodiscard]] static auto
    Create(Context &context, EncodeDevice const &device, VkExtent2D extent, std::size_t slot_count)
      -> std::expected<std::shared_ptr<GpuColorConverterPool>, Error>;
  [[nodiscard]] auto
    Convert(VkImage source, VkSemaphore ready_timeline, std::uint64_t ready_value, std::shared_ptr<void> source_owner)
      -> std::expected<ConvertedFrame, Error>;
  // Run on the recording owner thread after encoder completions or failures.
  // Waits for conversion before releasing an RGBA source. Recreates converters
  // whose binary semaphore was not consumed by an accepted encode submission.
  [[nodiscard]] auto RetireReleased() -> std::expected<void, Error>;
  [[nodiscard]] auto PollError() const -> std::optional<Error>;

private:
  friend class ConversionLease;
  enum class SlotState : std::uint8_t { kFree, kLeased, kRetiring, kRetiringGpu, kFailed };
  GpuColorConverterPool(Context &context, EncodeDevice device, VkExtent2D extent);
  auto Release(std::size_t slot, bool encode_submitted) noexcept -> void;

  Context &context_;
  EncodeDevice device_;
  VkExtent2D extent_{};
  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<void>> source_owners_;
  std::vector<std::unique_ptr<GpuColorConverter>> converters_;
  std::vector<SlotState> states_;
  std::vector<bool> encode_submitted_;
  std::optional<Error> failure_;
};

}// namespace vkgsplat::vulkan::video

#endif// VKGSPLAT_BACKEND_VULKAN_VIDEO_COLOR_CONVERTER_HPP
