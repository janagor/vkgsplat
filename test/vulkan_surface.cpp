#include "vulkan/vulkan_platform.hpp"

#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <system_error>
#include <vulkan/vulkan_core.h>

namespace {
class TestPlatform final : public vkgsplat::Platform
{
public:
  explicit TestPlatform(vkgsplat::SurfaceMode mode) : mode_(mode) {}
  [[nodiscard]] auto surface_mode() const noexcept -> vkgsplat::SurfaceMode override { return mode_; }
  [[nodiscard]] auto native_window() const noexcept -> vkgsplat::NativeWindowHandle override { return nullptr; }
  [[nodiscard]] auto framebuffer_extent() const noexcept -> vkgsplat::Extent2D override { return {}; }
  void poll_events() const noexcept override {}
  [[nodiscard]] auto should_close() const noexcept -> bool override { return false; }

private:
  vkgsplat::SurfaceMode mode_;
};
}// namespace

TEST_CASE("unknown surface mode fails before Vulkan calls", "[surface]")
{
  // Exercise defensive dispatch for values from an out-of-date Platform implementation.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  TestPlatform const platform{ static_cast<vkgsplat::SurfaceMode>(255) };
  auto result = vkgsplat::vulkan::CreateSurface(VK_NULL_HANDLE, platform);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().code() == std::errc::invalid_argument);
}

TEST_CASE("windowed mode rejects a null window handle", "[surface]")
{
  TestPlatform const platform{ vkgsplat::SurfaceMode::kWindowed };
  auto result = vkgsplat::vulkan::CreateSurface(VK_NULL_HANDLE, platform);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().code() == std::errc::invalid_argument);
}
