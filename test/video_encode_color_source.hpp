// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#ifndef VKGSPLAT_TEST_VIDEO_ENCODE_COLOR_SOURCE_HPP
#define VKGSPLAT_TEST_VIDEO_ENCODE_COLOR_SOURCE_HPP

#include "../src/vkgsplat_backend/vulkan/vulkan_context.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

namespace vkgsplat::test {

// A single-use RGBA8 transfer-source image with a timeline signal after upload.
class GpuColorSource
{
public:
  [[nodiscard]] static auto Create(vulkan::Context &context, VkExtent2D extent)
    -> std::expected<std::unique_ptr<GpuColorSource>, Error>;
  [[nodiscard]] auto Prepare() -> std::expected<void, Error>;
  [[nodiscard]] auto ReadNv12Buffer(VkBuffer source, VkDeviceSize size)
    -> std::expected<std::vector<std::byte>, Error>;
  [[nodiscard]] auto Image() const noexcept -> VkImage { return image_; }
  [[nodiscard]] auto ReadyTimeline() const noexcept -> VkSemaphore { return ready_; }
  [[nodiscard]] static auto ReadyValue() noexcept -> std::uint64_t { return kReadyValue; }

  ~GpuColorSource();
  GpuColorSource(GpuColorSource const &) = delete;
  auto operator=(GpuColorSource const &) -> GpuColorSource & = delete;
  GpuColorSource(GpuColorSource &&) = delete;
  auto operator=(GpuColorSource &&) -> GpuColorSource & = delete;

private:
  explicit GpuColorSource(vulkan::Context &context, VkExtent2D extent) noexcept;

  static constexpr std::uint64_t kReadyValue = 1U;
  vulkan::Context &context_;
  VkExtent2D extent_{};
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkImage image_ = VK_NULL_HANDLE;
  VkDeviceMemory image_memory_ = VK_NULL_HANDLE;
  VkBuffer upload_ = VK_NULL_HANDLE;
  VkDeviceMemory upload_memory_ = VK_NULL_HANDLE;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_ = VK_NULL_HANDLE;
  VkSemaphore ready_ = VK_NULL_HANDLE;
  bool submitted_ = false;
};

}// namespace vkgsplat::test

#endif// VKGSPLAT_TEST_VIDEO_ENCODE_COLOR_SOURCE_HPP
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
