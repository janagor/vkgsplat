#ifndef VKGSPLAT_BACKEND_VULKAN_SWAPCHAIN_RESOURCE_HPP
#define VKGSPLAT_BACKEND_VULKAN_SWAPCHAIN_RESOURCE_HPP

#include <expected>
#include <functional>
#include <vector>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

class SwapchainResource
{
public:
  SwapchainResource() = delete;
  ~SwapchainResource() noexcept;

  SwapchainResource(SwapchainResource const &) = delete;
  auto operator=(SwapchainResource const &) -> SwapchainResource & = delete;

  SwapchainResource(SwapchainResource &&other) noexcept;
  auto operator=(SwapchainResource &&other) noexcept -> SwapchainResource &;

  [[nodiscard]] static auto create(vkb::Device const &device,
    Extent2D extent,
    std::reference_wrapper<vkb::DispatchTable> disp) -> std::expected<SwapchainResource, Error>;

  [[nodiscard]] auto recreate(vkb::Device const &device, Extent2D extent) -> std::expected<void, Error>;

  [[nodiscard]] auto handle() const noexcept -> VkSwapchainKHR { return swapchain_.swapchain; }
  [[nodiscard]] auto format() const noexcept -> VkFormat { return swapchain_.image_format; }
  [[nodiscard]] auto extent() const noexcept -> VkExtent2D const & { return swapchain_.extent; }
  [[nodiscard]] auto image_count() const noexcept -> u32 { return swapchain_.image_count; }
  [[nodiscard]] auto images() const noexcept -> std::vector<VkImage> const & { return images_; }
  [[nodiscard]] auto image_views() const noexcept -> std::vector<VkImageView> const & { return image_views_; }

private:
  SwapchainResource(vkb::Swapchain swapchain, std::reference_wrapper<vkb::DispatchTable> disp) noexcept;

  void cleanup() noexcept;
  [[nodiscard]] auto init_images_and_views() -> std::expected<void, Error>;

  vkb::Swapchain swapchain_;
  std::vector<VkImage> images_;
  std::vector<VkImageView> image_views_;
  std::reference_wrapper<vkb::DispatchTable> disp_;
};

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_SWAPCHAIN_RESOURCE_HPP
