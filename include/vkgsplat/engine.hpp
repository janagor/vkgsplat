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

/** Options used when creating the engine hardware context. */
struct EngineConfig
{
  bool enable_validation = false;
  /// Request VK_EXT_present_timing; ignored when the device does not support it.
  bool request_present_timing = false;
};

/**
 * Main library entry point.
 *
 * Engine owns the Driver and the hardware context. It borrows the Platform,
 * which must outlive the Engine. Destroy renderers before destroying the
 * Engine. Factory and renderer creation errors are returned as `std::expected`.
 */
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

  /** Create a renderer that shares this engine's driver. */
  [[nodiscard]] auto create_renderer(RendererConfig const &config) -> std::expected<Renderer, Error>;

  /** Wait until all work submitted through the driver has finished. */
  void wait_idle() const noexcept;

private:
  struct Impl;
  explicit Engine(beman::indirect::indirect<Impl> impl);

  beman::indirect::indirect<Impl> impl_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_ENGINE_HPP
