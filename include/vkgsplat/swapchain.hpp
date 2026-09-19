#ifndef VKGSPLAT_SWAPCHAIN_HPP
#define VKGSPLAT_SWAPCHAIN_HPP

#include <expected>

#include <vkgsplat/vkgsplat_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

/** Abstract renderable presentation target owned by a graphics backend. */
class VKGSPLAT_EXPORT Swapchain
{
public:
  virtual ~Swapchain() = default;

  Swapchain(Swapchain const &) = delete;
  auto operator=(Swapchain const &) -> Swapchain & = delete;

  [[nodiscard]] virtual auto extent() const noexcept -> Extent2D = 0;
  [[nodiscard]] virtual auto image_count() const noexcept -> u32 = 0;
  /** Recreate the target for a new framebuffer extent. */
  [[nodiscard]] virtual auto recreate(Extent2D extent) -> std::expected<void, Error> = 0;

protected:
  Swapchain() = default;
  Swapchain(Swapchain &&) noexcept = default;
  auto operator=(Swapchain &&) noexcept -> Swapchain & = default;
};

}// namespace vkgsplat

#endif// VKGSPLAT_SWAPCHAIN_HPP
