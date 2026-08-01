#include "presentable_swapchain.hpp"

#include "swapchain_resource.hpp"

#include <expected>
#include <functional>
#include <memory>
#include <utility>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

namespace vkgsplat::vulkan {

PresentableSwapchain::PresentableSwapchain(std::unique_ptr<SwapchainResource> swapchain, vkb::Device device) noexcept
  : swapchain_(std::move(swapchain)), device_(std::move(device))
{}

auto PresentableSwapchain::create(vkb::Device const &device,
  Extent2D extent,
  std::reference_wrapper<vkb::DispatchTable> disp) -> std::expected<std::unique_ptr<PresentableSwapchain>, Error>
{
  auto resource = SwapchainResource::create(device, extent, disp);
  if (!resource) { return std::unexpected(resource.error()); }
  return std::unique_ptr<PresentableSwapchain>(
    new PresentableSwapchain(std::make_unique<SwapchainResource>(std::move(*resource)), device));
}

auto PresentableSwapchain::extent() const noexcept -> Extent2D
{
  auto const &native_extent = swapchain_->extent();
  return Extent2D{ .width = native_extent.width, .height = native_extent.height };
}

auto PresentableSwapchain::image_count() const noexcept -> u32 { return swapchain_->image_count(); }

auto PresentableSwapchain::recreate(Extent2D extent) -> std::expected<void, Error>
{ return recreate(device_, extent); }

auto PresentableSwapchain::recreate(vkb::Device const &device, Extent2D extent) -> std::expected<void, Error>
{ return swapchain_->recreate(device, extent); }

}// namespace vkgsplat::vulkan
