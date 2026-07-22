#include "backend/vulkan/swapchain.hpp"

#include <expected>
#include <functional>
#include <utility>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "backend/vulkan/vulkan_bootstrap.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

namespace {

  auto build_swapchain(vkb::Device const &device, GLFWwindow *window, vkb::Swapchain const &old_swapchain = {})
    -> std::expected<vkb::Swapchain, Error>
  {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    vkb::SwapchainBuilder swapchain_builder{ device };
    return VKBResultToExpected(swapchain_builder.set_desired_extent(static_cast<u32>(width), static_cast<u32>(height))
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .set_old_swapchain(old_swapchain)
        .build());
  }

}// namespace

Swapchain::Swapchain(vkb::Swapchain swapchain, std::reference_wrapper<vkb::DispatchTable> disp) noexcept
  : swapchain_(swapchain), disp_(disp)
{}

Swapchain::Swapchain(Swapchain &&other) noexcept
  : swapchain_(other.swapchain_), images_(std::move(other.images_)), image_views_(std::move(other.image_views_)),
    disp_(other.disp_)
{ other.swapchain_.swapchain = VK_NULL_HANDLE; }

auto Swapchain::operator=(Swapchain &&other) noexcept -> Swapchain &
{
  if (this != &other) {
    cleanup();
    swapchain_ = other.swapchain_;
    images_ = std::move(other.images_);
    image_views_ = std::move(other.image_views_);
    disp_ = other.disp_;
    other.swapchain_.swapchain = VK_NULL_HANDLE;
  }
  return *this;
}

Swapchain::~Swapchain() { cleanup(); }

void Swapchain::cleanup() noexcept
{
  if (swapchain_.swapchain != VK_NULL_HANDLE) {
    swapchain_.destroy_image_views(image_views_);
    vkb::destroy_swapchain(swapchain_);
    swapchain_.swapchain = VK_NULL_HANDLE;
  }
  image_views_.clear();
  images_.clear();
}

auto Swapchain::init_images_and_views() -> std::expected<void, Error>
{
  auto swapchain_images = VKBResultToExpected(swapchain_.get_images());
  if (!swapchain_images) { return std::unexpected(swapchain_images.error()); }
  images_ = std::move(*swapchain_images);

  auto swapchain_image_views = VKBResultToExpected(swapchain_.get_image_views());
  if (!swapchain_image_views) { return std::unexpected(swapchain_image_views.error()); }
  image_views_ = std::move(*swapchain_image_views);

  return {};
}

auto Swapchain::create(vkb::Device const &device, GLFWwindow *window, std::reference_wrapper<vkb::DispatchTable> disp)
  -> std::expected<Swapchain, Error>
{
  auto vkb_swapchain = build_swapchain(device, window);
  if (!vkb_swapchain) { return std::unexpected(vkb_swapchain.error()); }

  Swapchain swapchain(*vkb_swapchain, disp);
  if (auto images_and_views = swapchain.init_images_and_views(); !images_and_views) {
    return std::unexpected(images_and_views.error());
  }
  return swapchain;
}

auto Swapchain::recreate(vkb::Device const &device, GLFWwindow *window) -> std::expected<void, Error>
{
  vkb::Swapchain const old_swapchain = swapchain_;
  auto vkb_swapchain = build_swapchain(device, window, old_swapchain);
  if (!vkb_swapchain) { return std::unexpected(vkb_swapchain.error()); }

  cleanup();
  swapchain_ = *vkb_swapchain;
  return init_images_and_views();
}

}// namespace vkgsplat::vulkan
