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
#include <vector>

namespace vkgsplat {

/** Configuration for a renderer and its optional quilt output. */
struct RendererConfig
{
  std::string ply_path;
  bool enable_validation = false;
  bool enable_imgui = true;
  bool enable_gpu_timers = false;
  FrameRateConfig frame_rate{};
  /// Quilt dimensions as {columns, rows}; {1, 1} selects the mono path.
  std::array<u32, 2> lfd_grid{ 1U, 1U };
  f64 view_cone_deg{ kDefaultViewConeDegrees };
  std::vector<u32> lfd_view_order;
};

/**
 * Per-window rendering context.
 *
 * Renderer owns frame resources, command submission, presentation, and the
 * loaded Gaussian splat data. It borrows the Platform and, when created from
 * an Engine, the Engine's driver. All operations return `std::expected` for
 * recoverable failures.
 */
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

  /** Render one frame using the supplied camera. */
  [[nodiscard]] auto draw(Camera const &camera) -> std::expected<void, Error>;
  /** Save the latest scene color target as PNG, excluding the ImGui overlay. */
  [[nodiscard]] auto save_frame_png(std::string_view path) -> std::expected<void, Error>;
  void wait_idle() noexcept;

  /** Render one quilt cell full-screen instead of the complete atlas. */
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
