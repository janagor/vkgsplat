#include "swapchain_resource.hpp"

#include <expected>
#include <functional>
#include <utility>
#include <vector>

#include "vulkan_bootstrap.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

namespace {

  auto BuildSwapchainResource(vkb::Device const &device,
    Extent2D ext,
    SwapchainCreateOptions const &options,
    vkb::Swapchain const &old_swapchain = {}) -> std::expected<vkb::Swapchain, Error>
  {
    vkb::SwapchainBuilder swapchain_builder{ device };
    // 3DGS SH colors are trained in gamma / sRGB space and blended in a UNORM color target.
    // The blit is a raw copy - an _SRGB swapchain would apply gamma again (double-gamma).
    swapchain_builder.set_desired_extent(ext.width, ext.height)
      .set_desired_format({ .format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
      .add_fallback_format({ .format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
      .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
      .set_old_swapchain(old_swapchain);

    if (options.enable_present_timing) {
      // Scheduled presents require FIFO (or FIFO_RELAXED / FIFO_LATEST_READY).
      swapchain_builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
      VkFlags create_flags = VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
      if (options.enable_present_id2) { create_flags |= VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR; }
      // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
      swapchain_builder.set_create_flags(static_cast<VkSwapchainCreateFlagBitsKHR>(create_flags));
    }

    return VKBResultToExpected(swapchain_builder.build());
  }

}// namespace

SwapchainResource::SwapchainResource(vkb::Swapchain swapchain,
  std::reference_wrapper<vkb::DispatchTable> disp,
  bool present_timing_enabled,
  bool present_id2_enabled) noexcept
  : swapchain_(swapchain), disp_(disp), present_timing_enabled_(present_timing_enabled),
    present_id2_enabled_(present_id2_enabled)
{}

SwapchainResource::SwapchainResource(SwapchainResource &&other) noexcept
  : swapchain_(std::exchange(other.swapchain_, {})), images_(std::move(other.images_)),
    image_views_(std::move(other.image_views_)), disp_(other.disp_),
    present_timing_enabled_(other.present_timing_enabled_), present_id2_enabled_(other.present_id2_enabled_)
{}

auto SwapchainResource::operator=(SwapchainResource &&other) noexcept -> SwapchainResource &
{
  if (this != &other) {
    cleanup();
    swapchain_ = std::exchange(other.swapchain_, {});
    images_ = std::move(other.images_);
    image_views_ = std::move(other.image_views_);
    disp_ = other.disp_;
    present_timing_enabled_ = other.present_timing_enabled_;
    present_id2_enabled_ = other.present_id2_enabled_;
  }
  return *this;
}

SwapchainResource::~SwapchainResource() noexcept { cleanup(); }

void SwapchainResource::cleanup() noexcept
{
  if (swapchain_.swapchain != VK_NULL_HANDLE) {
    swapchain_.destroy_image_views(image_views_);
    vkb::destroy_swapchain(swapchain_);
    swapchain_.swapchain = VK_NULL_HANDLE;
  }
  image_views_.clear();
  images_.clear();
}

auto SwapchainResource::init_images_and_views() -> std::expected<void, Error>
{
  auto swapchain_images = VKBResultToExpected(swapchain_.get_images());
  if (!swapchain_images) { return std::unexpected(swapchain_images.error()); }
  images_ = std::move(*swapchain_images);

  auto swapchain_image_views = VKBResultToExpected(swapchain_.get_image_views());
  if (!swapchain_image_views) { return std::unexpected(swapchain_image_views.error()); }
  image_views_ = std::move(*swapchain_image_views);

  return {};
}

auto SwapchainResource::create(vkb::Device const &device,
  Extent2D ext,
  std::reference_wrapper<vkb::DispatchTable> disp,
  SwapchainCreateOptions const &options) -> std::expected<SwapchainResource, Error>
{
  auto vkb_swapchain = BuildSwapchainResource(device, ext, options);
  if (!vkb_swapchain) { return std::unexpected(vkb_swapchain.error()); }

  SwapchainResource swapchain(*vkb_swapchain, disp, options.enable_present_timing, options.enable_present_id2);
  if (auto images_and_views = swapchain.init_images_and_views(); !images_and_views) {
    return std::unexpected(images_and_views.error());
  }
  return swapchain;
}

auto SwapchainResource::recreate(vkb::Device const &device, Extent2D ext) -> std::expected<void, Error>
{
  vkb::Swapchain const old_swapchain = swapchain_;
  SwapchainCreateOptions const options{ .enable_present_timing = present_timing_enabled_,
    .enable_present_id2 = present_id2_enabled_ };
  auto vkb_swapchain = BuildSwapchainResource(device, ext, options, old_swapchain);
  if (!vkb_swapchain) { return std::unexpected(vkb_swapchain.error()); }

  cleanup();
  swapchain_ = *vkb_swapchain;
  if (auto images_and_views = init_images_and_views(); !images_and_views) {
    return std::unexpected(images_and_views.error());
  }
  return {};
}

}// namespace vkgsplat::vulkan
