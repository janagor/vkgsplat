#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <vector>

#include <vkgsplat/swapchain.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include "swapchain_resource.hpp"

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

// Abstract Swapchain façade over the concrete Vulkan swapchain RAII type.
class PresentableSwapchain final : public vkgsplat::Swapchain
{
public:
  [[nodiscard]] static auto create(vkb::Device const &device,
    Extent2D extent,
    std::reference_wrapper<vkb::DispatchTable> disp) -> std::expected<std::unique_ptr<PresentableSwapchain>, Error>;

  [[nodiscard]] auto extent() const noexcept -> Extent2D override;
  [[nodiscard]] auto image_count() const noexcept -> u32 override;
  [[nodiscard]] auto recreate(Extent2D extent) -> std::expected<void, Error> override;

  [[nodiscard]] auto recreate(vkb::Device const &device, Extent2D extent) -> std::expected<void, Error>;

  [[nodiscard]] auto handle() const noexcept -> VkSwapchainKHR { return swapchain_->handle(); }
  [[nodiscard]] auto format() const noexcept -> VkFormat { return swapchain_->format(); }
  [[nodiscard]] auto vk_extent() const noexcept -> VkExtent2D const & { return swapchain_->extent(); }
  [[nodiscard]] auto images() const noexcept -> std::vector<VkImage> const & { return swapchain_->images(); }
  [[nodiscard]] auto image_views() const noexcept -> std::vector<VkImageView> const &
  { return swapchain_->image_views(); }

private:
  PresentableSwapchain(std::unique_ptr<SwapchainResource> swapchain, vkb::Device device) noexcept;

  std::unique_ptr<SwapchainResource> swapchain_;
  vkb::Device device_{};
};

}// namespace vkgsplat::vulkan
