#ifndef VKGSPLAT_BACKEND_VULKAN_VIDEO_COLOR_CONVERTER_HPP
#define VKGSPLAT_BACKEND_VULKAN_VIDEO_COLOR_CONVERTER_HPP

#include "video_encoder.hpp"

#include <cstdint>
#include <expected>
#include <memory>

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

}// namespace vkgsplat::vulkan::video

#endif// VKGSPLAT_BACKEND_VULKAN_VIDEO_COLOR_CONVERTER_HPP
