#include <vkgsplat/platform.hpp>
#include <vkgsplat_headless/headless_platform.hpp>
#include <vkgsplat_utility/types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <system_error>

namespace {
constexpr vkgsplat::u32 kTestWidth = 1280;
constexpr vkgsplat::u32 kTestHeight = 720;
}// namespace

TEST_CASE("headless platform reports fixed extent without a window", "[headless]")
{
  auto platform = vkgsplat::HeadlessPlatform::create({ .width = kTestWidth, .height = kTestHeight });
  REQUIRE(platform.has_value());
  REQUIRE(platform->surface_mode() == vkgsplat::SurfaceMode::kHeadless);
  REQUIRE(platform->native_window() == nullptr);
  REQUIRE(platform->framebuffer_extent().width == kTestWidth);
  REQUIRE(platform->framebuffer_extent().height == kTestHeight);
  platform->poll_events();
  platform->post_empty_event();
  REQUIRE_FALSE(platform->should_close());
  REQUIRE(platform->framebuffer_extent().width == kTestWidth);
}

TEST_CASE("headless platform rejects zero dimensions", "[headless]")
{
  auto zero_width = vkgsplat::HeadlessPlatform::create({ .width = 0, .height = kTestHeight });
  auto zero_height = vkgsplat::HeadlessPlatform::create({ .width = kTestWidth, .height = 0 });
  REQUIRE_FALSE(zero_width.has_value());
  REQUIRE_FALSE(zero_height.has_value());
  REQUIRE(zero_width.error().code() == std::errc::invalid_argument);
  REQUIRE(zero_height.error().code() == std::errc::invalid_argument);
}

TEST_CASE("platform defaults to windowed mode", "[headless]")
{
  struct DefaultPlatform final : vkgsplat::Platform
  {
    [[nodiscard]] auto native_window() const noexcept -> vkgsplat::NativeWindowHandle override { return nullptr; }
    [[nodiscard]] auto framebuffer_extent() const noexcept -> vkgsplat::Extent2D override { return {}; }
    void poll_events() const noexcept override {}
    [[nodiscard]] auto should_close() const noexcept -> bool override { return false; }
  } const platform;
  REQUIRE(platform.surface_mode() == vkgsplat::SurfaceMode::kWindowed);
}
