#include "vulkan_driver.hpp"

#include <expected>
#include <functional>
#include <memory>
#include <utility>

#include <vkgsplat/driver.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/swapchain.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include "device.hpp"
#include "gpu_allocator.hpp"
#include "presentable_swapchain.hpp"

#include <VkBootstrap.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

VulkanDriver::VulkanDriver(Platform &platform) noexcept { context_.platform = &platform; }

VulkanDriver::~VulkanDriver()
{
  context_.swapchain.reset();
  context_.gpu_allocator = GPUAllocator{};
  if (context_.device.device != VK_NULL_HANDLE) {
    context_.disp.deviceWaitIdle();
    vkb::destroy_device(context_.device);
    context_.device = {};
  }
  if (context_.surface != VK_NULL_HANDLE && context_.instance.instance != VK_NULL_HANDLE) {
    vkb::destroy_surface(context_.instance, context_.surface);
    context_.surface = VK_NULL_HANDLE;
  }
  if (context_.instance.instance != VK_NULL_HANDLE) {
    vkb::destroy_instance(context_.instance);
    context_.instance = {};
  }
}

auto VulkanDriver::create(Platform &platform, DriverConfig const &config)
  -> std::expected<std::unique_ptr<VulkanDriver>, Error>
{
  auto driver = std::unique_ptr<VulkanDriver>(new VulkanDriver(platform));
  if (auto initialized = device_initialization(driver->context_, config.enable_validation); !initialized) {
    return std::unexpected(initialized.error());
  }

  auto gpu_allocator =
    GPUAllocator::create(driver->context_.instance, driver->context_.device, driver->context_.device.physical_device);
  if (!gpu_allocator) { return std::unexpected(gpu_allocator.error()); }
  driver->context_.gpu_allocator = std::move(*gpu_allocator);

  if (auto created = driver->create_swapchain(platform, platform.framebuffer_extent()); !created) {
    return std::unexpected(created.error());
  }

  return driver;
}

auto VulkanDriver::create_swapchain(Platform &platform, Extent2D extent) -> std::expected<void, Error>
{
  (void)platform;
  auto created = PresentableSwapchain::create(context_.device, extent, std::ref(context_.disp));
  if (!created) { return std::unexpected(created.error()); }
  context_.swapchain = std::move(*created);
  return {};
}

auto VulkanDriver::swapchain() noexcept -> vkgsplat::Swapchain * { return context_.swapchain.get(); }

auto VulkanDriver::swapchain() const noexcept -> vkgsplat::Swapchain const * { return context_.swapchain.get(); }

void VulkanDriver::wait_idle() const noexcept
{
  if (context_.device.device != VK_NULL_HANDLE) { context_.disp.deviceWaitIdle(); }
}

}// namespace vkgsplat::vulkan

namespace vkgsplat {

auto create_vulkan_driver(Platform &platform, DriverConfig const &config)
  -> std::expected<std::unique_ptr<Driver>, Error>
{
  auto driver = vulkan::VulkanDriver::create(platform, config);
  if (!driver) { return std::unexpected(driver.error()); }
  return std::unique_ptr<Driver>{ std::move(*driver) };
}

}// namespace vkgsplat
