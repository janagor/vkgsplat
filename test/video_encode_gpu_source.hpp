// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#ifndef VKGSPLAT_TEST_VIDEO_ENCODE_GPU_SOURCE_HPP
#define VKGSPLAT_TEST_VIDEO_ENCODE_GPU_SOURCE_HPP

#include "../src/vkgsplat_backend/vulkan/video/video_encoder.hpp"
#include "../src/vkgsplat_backend/vulkan/vulkan_context.hpp"

#include <cstdint>
#include <expected>
#include <memory>

namespace vkgsplat::test {

// Test producer for an external NV12 image. EncodeImage must consume each
// signaled semaphore before Prepare is called again.
class GpuVideoSource
{
public:
  [[nodiscard]] static auto Create(vulkan::Context &context,
    vulkan::video::EncodeDevice const &device,
    VkExtent2D extent) -> std::expected<std::unique_ptr<GpuVideoSource>, Error>;
  [[nodiscard]] auto Prepare(std::uint8_t luma) -> std::expected<vulkan::video::PreparedVideoFrame, Error>;
  auto Complete() noexcept -> void { in_flight_ = false; }

  ~GpuVideoSource();
  GpuVideoSource(GpuVideoSource const &) = delete;
  auto operator=(GpuVideoSource const &) -> GpuVideoSource & = delete;
  GpuVideoSource(GpuVideoSource &&) = delete;
  auto operator=(GpuVideoSource &&) -> GpuVideoSource & = delete;

private:
  GpuVideoSource(vulkan::Context &context, VkExtent2D extent) noexcept;

  vulkan::Context &context_;
  VkExtent2D extent_{};
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkImage image_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  VkDeviceMemory image_memory_ = VK_NULL_HANDLE;
  VkBuffer upload_ = VK_NULL_HANDLE;
  VkDeviceMemory upload_memory_ = VK_NULL_HANDLE;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  VkSemaphore ready_ = VK_NULL_HANDLE;
  bool in_flight_ = false;
};

}// namespace vkgsplat::test

#endif// VKGSPLAT_TEST_VIDEO_ENCODE_GPU_SOURCE_HPP
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
