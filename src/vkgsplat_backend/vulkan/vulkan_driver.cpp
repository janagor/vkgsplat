#include "vulkan_driver.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

#include <vkgsplat/driver.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/swapchain.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include "device.hpp"
#include "presentable_swapchain.hpp"
#include "swapchain_resource.hpp"
#include "vulkan_context.hpp"

#include <VkBootstrap.h>
#include <vkexec/context.hpp>
#include <vkexec/sync_wait.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {
namespace {

  auto AdoptVkexecContext(Context &context) -> std::expected<void, Error>
  {
    auto graphics = context.device.get_queue_and_index(vkb::QueueType::graphics);
    if (!graphics) {
      return std::unexpected{ Error{ std::make_error_code(std::errc::function_not_supported),
        "graphics queue unavailable for vkexec adopt" } };
    }

    auto present = context.device.get_queue_and_index(vkb::QueueType::present);
    VkQueue present_queue = present ? present->first : graphics->first;
    std::uint32_t present_family = present ? present->second : graphics->second;

    // Prefer a dedicated compute queue when present; otherwise graphics (compute-capable).
    VkQueue compute_queue = graphics->first;
    std::uint32_t compute_family = graphics->second;
    if (auto compute = context.device.get_queue_and_index(vkb::QueueType::compute)) {
      compute_queue = compute->first;
      compute_family = compute->second;
    }

    // Null allocator: vkexec creates and owns VMA (BUFFER_DEVICE_ADDRESS).
    auto adopted = vkexec::try_sync_wait_value(vkexec::context::adopt({
      .instance = context.instance.instance,
      .physical_device = context.device.physical_device,
      .device = context.device.device,
      .allocator = VK_NULL_HANDLE,
      .compute_queue = compute_queue,
      .compute_queue_family = compute_family,
      .graphics_queue = graphics->first,
      .graphics_queue_family = graphics->second,
      .present_queue = present_queue,
      .present_queue_family = present_family,
    }));
    if (!adopted) {
      return std::unexpected{ Error{ std::make_error_code(std::errc::invalid_argument), adopted.error().message() } };
    }
    context.vkexec_context = std::move(*adopted);
    return {};
  }

}// namespace

VulkanDriver::VulkanDriver(Platform &platform) noexcept { context_.platform = &platform; }

VulkanDriver::~VulkanDriver()
{
  context_.swapchain.reset();
  // vkexec owns VMA, command pool, and pipeline cache; drop before device.
  context_.vkexec_context.reset();
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
  if (auto initialized = DeviceInitialization(driver->context_, config); !initialized) {
    return std::unexpected(initialized.error());
  }

  if (auto adopted = AdoptVkexecContext(driver->context_); !adopted) {
    return std::unexpected(adopted.error());
  }

  if (auto created = driver->create_swapchain(platform, platform.framebuffer_extent()); !created) {
    return std::unexpected(created.error());
  }

  return driver;
}

auto VulkanDriver::create_swapchain(Platform &platform, Extent2D extent) -> std::expected<void, Error>
{
  (void)platform;
  SwapchainCreateOptions const options{ .enable_present_timing = context_.present_timing_enabled,
    .enable_present_id2 = context_.present_id2_enabled };
  auto created = PresentableSwapchain::create(context_.device, extent, std::ref(context_.disp), options);
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

auto CreateVulkanDriver(Platform &platform, DriverConfig const &config)
  -> std::expected<std::unique_ptr<Driver>, Error>
{
  auto driver = vulkan::VulkanDriver::create(platform, config);
  if (!driver) { return std::unexpected(driver.error()); }
  return std::unique_ptr<Driver>{ std::move(*driver) };
}

}// namespace vkgsplat
