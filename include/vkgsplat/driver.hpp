#ifndef VKGSPLAT_DRIVER_HPP
#define VKGSPLAT_DRIVER_HPP

#include <expected>
#include <memory>

#include <vkgsplat/platform.hpp>
#include <vkgsplat/swapchain.hpp>
#include <vkgsplat/vkgsplat_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

/** Options used while creating a graphics driver. */
struct DriverConfig
{
  bool enable_validation = false;
  bool request_present_timing = false;
};

/**
 * Low-level graphics API abstraction.
 *
 * Concrete backends live under `src/vkgsplat_backend/<api>/`. A Driver owns
 * backend state and the current Swapchain, but does not own the Platform.
 */
class VKGSPLAT_EXPORT Driver
{
public:
  virtual ~Driver() = default;

  Driver(Driver const &) = delete;
  auto operator=(Driver const &) -> Driver & = delete;

  /** Create or replace the presentation target for `platform` and `extent`. */
  [[nodiscard]] virtual auto create_swapchain(Platform &platform, Extent2D extent) -> std::expected<void, Error> = 0;
  [[nodiscard]] virtual auto swapchain() noexcept -> Swapchain * = 0;
  [[nodiscard]] virtual auto swapchain() const noexcept -> Swapchain const * = 0;

  virtual void wait_idle() const noexcept = 0;

protected:
  Driver() = default;
  Driver(Driver &&) noexcept = default;
  auto operator=(Driver &&) noexcept -> Driver & = default;
};

/** Create the Vulkan implementation and return an error without throwing. */
[[nodiscard]] VKGSPLAT_EXPORT auto CreateVulkanDriver(Platform &platform, DriverConfig const &config)
  -> std::expected<std::unique_ptr<Driver>, Error>;

}// namespace vkgsplat

#endif// VKGSPLAT_DRIVER_HPP
