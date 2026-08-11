#ifndef VKGSPLAT_RENDERER_HPP
#define VKGSPLAT_RENDERER_HPP

#include <expected>
#include <string>
#include <string_view>

#include <beman/indirect/indirect.hpp>
#include <vkgsplat/camera.hpp>
#include <vkgsplat/engine.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat/lfd_config.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/vkgsplat_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>

namespace vkgsplat {

struct RendererConfig
{
  std::string ply_path;
  bool enable_validation = false;
  bool enable_imgui = true;
  bool enable_gpu_timers = false;
  FrameRateConfig frame_rate{};
  // LFD quilt; {1,1} keeps the mono path. [columns, rows]
  std::array<u32, 2> lfd_grid{ 1U, 1U };
  f64 view_cone_deg{ kDefaultViewConeDegrees };
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
  // Reads the latest raster color target to PNG (scene only; no ImGui overlay).
  [[nodiscard]] auto save_frame_png(std::string_view path) -> std::expected<void, Error>;
  void wait_idle() noexcept;

  // LFD emulate: render a single quilt view full-screen instead of the full atlas.
  void set_lfd_emulate(bool active);
  [[nodiscard]] auto lfd_emulate_active() const noexcept -> bool;
  void set_lfd_emulate_cell(u32 col, u32 row);
  [[nodiscard]] auto lfd_emulate_cell() const noexcept -> std::array<u32, 2>;
  [[nodiscard]] auto lfd_grid() const noexcept -> std::array<u32, 2>;

private:
  struct Impl;
  explicit Renderer(beman::indirect::indirect<Impl> impl);

  beman::indirect::indirect<Impl> impl_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_RENDERER_HPP
