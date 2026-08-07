#ifndef VKGSPLAT_ENGINE_HPP
#define VKGSPLAT_ENGINE_HPP

#include <expected>
#include <memory>
#include <string>

#include <beman/indirect/indirect.hpp>
#include <vkgsplat/driver.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/vkgsplat_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

class Renderer;
struct RendererConfig;

struct EngineConfig
{
  bool enable_validation = false;
  // Request VK_EXT_present_timing when creating the driver (optional; ignored if unsupported).
  bool request_present_timing = false;
};

// Main entry point: owns the Driver (hardware context) and tracks user-facing
// render resources created through it.
class VKGSPLAT_EXPORT Engine
{
public:
  [[nodiscard]] static auto create(EngineConfig const &config, Platform &platform) -> std::expected<Engine, Error>;

  Engine(Engine &&) noexcept;
  auto operator=(Engine &&) noexcept -> Engine &;
  ~Engine() noexcept;

  Engine(Engine const &) = delete;
  auto operator=(Engine const &) -> Engine & = delete;

  [[nodiscard]] auto driver() noexcept -> Driver &;
  [[nodiscard]] auto driver() const noexcept -> Driver const &;
  [[nodiscard]] auto platform() noexcept -> Platform &;
  [[nodiscard]] auto platform() const noexcept -> Platform const &;

  [[nodiscard]] auto create_renderer(RendererConfig const &config) -> std::expected<Renderer, Error>;

  void wait_idle() const noexcept;

private:
  struct Impl;
  explicit Engine(beman::indirect::indirect<Impl> impl);

  beman::indirect::indirect<Impl> impl_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_ENGINE_HPP
