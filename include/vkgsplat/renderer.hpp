#pragma once

#include <expected>
#include <string>

#include <beman/indirect/indirect.hpp>
#include <vkgsplat/camera.hpp>
#include <vkgsplat/engine.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/vkgsplat_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

struct RendererConfig
{
  u32 splat_count = 64;
  std::string ply_path;
  bool enable_validation = false;
  bool enable_imgui = true;
  bool enable_gpu_timers = false;
};

// Per-window rendering context: frame latency, command submission, and presentation.
class VKGSPLAT_EXPORT Renderer
{
public:
  [[nodiscard]] static auto create(RendererConfig const &config, Platform &platform) -> std::expected<Renderer, Error>;
  [[nodiscard]] static auto create(RendererConfig const &config, Engine &engine) -> std::expected<Renderer, Error>;

  Renderer(Renderer &&) noexcept;
  auto operator=(Renderer &&) noexcept -> Renderer &;
  ~Renderer() noexcept;

  Renderer(Renderer const &) = delete;
  auto operator=(Renderer const &) -> Renderer & = delete;

  [[nodiscard]] auto draw(Camera const &camera) -> std::expected<void, Error>;
  void wait_idle() const noexcept;

private:
  struct Impl;
  explicit Renderer(beman::indirect::indirect<Impl> impl);

  beman::indirect::indirect<Impl> impl_;
};

}// namespace vkgsplat
