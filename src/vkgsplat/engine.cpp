#include <vkgsplat/engine.hpp>

#include <expected>
#include <memory>
#include <utility>

#include <beman/indirect/indirect.hpp>

#include <vkgsplat/driver.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

struct Engine::Impl
{
  Platform *platform{};
  std::unique_ptr<Driver> driver;
};

Engine::Engine(beman::indirect::indirect<Impl> impl) : impl_(std::move(impl)) {}

Engine::Engine(Engine &&) noexcept = default;
auto Engine::operator=(Engine &&) noexcept -> Engine & = default;

Engine::~Engine() noexcept = default;

auto Engine::create(EngineConfig const &config, Platform &platform) -> std::expected<Engine, Error>
{
  beman::indirect::indirect<Impl> impl;
  impl->platform = &platform;

  auto created_driver = create_vulkan_driver(platform, DriverConfig{ .enable_validation = config.enable_validation });
  if (!created_driver) { return std::unexpected(created_driver.error()); }
  impl->driver = std::move(*created_driver);

  return Engine{ std::move(impl) };
}

auto Engine::driver() noexcept -> Driver & { return *impl_->driver; }

auto Engine::driver() const noexcept -> Driver const & { return *impl_->driver; }

auto Engine::platform() noexcept -> Platform & { return *impl_->platform; }

auto Engine::platform() const noexcept -> Platform const & { return *impl_->platform; }

void Engine::wait_idle() const noexcept { impl_->driver->wait_idle(); }

}// namespace vkgsplat
