#pragma once

#include <expected>
#include <memory>

#include <vkgsplat/driver.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/swapchain.hpp>
#include <vkgsplat_utility/error.hpp>

#include "vulkan_context.hpp"

namespace vkgsplat::vulkan {

class VulkanDriver final : public Driver
{
public:
  [[nodiscard]] static auto create(Platform &platform, DriverConfig const &config)
    -> std::expected<std::unique_ptr<VulkanDriver>, Error>;

  ~VulkanDriver() override;

  [[nodiscard]] auto create_swapchain(Platform &platform, Extent2D extent) -> std::expected<void, Error> override;
  [[nodiscard]] auto swapchain() noexcept -> vkgsplat::Swapchain * override;
  [[nodiscard]] auto swapchain() const noexcept -> vkgsplat::Swapchain const * override;

  void wait_idle() const noexcept override;

  [[nodiscard]] auto context() noexcept -> Init & { return context_; }
  [[nodiscard]] auto context() const noexcept -> Init const & { return context_; }

  [[nodiscard]] auto init() noexcept -> Init & { return context_; }
  [[nodiscard]] auto init() const noexcept -> Init const & { return context_; }

private:
  explicit VulkanDriver(Platform &platform) noexcept;

  Init context_{};
};

}// namespace vkgsplat::vulkan
