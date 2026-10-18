#ifndef VKGSPLAT_RENDERER_HPP
#define VKGSPLAT_RENDERER_HPP

#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <beman/indirect/indirect.hpp>
#include <vkgsplat/camera.hpp>
#include <vkgsplat/engine.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/lfd_config.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/vkgsplat_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vkgsplat {

enum class FrameSubmissionPolicy : u8 { kLatest, kLossless };

struct RenderDeviceInfo
{
  std::string name;
  u32 vendor_id{};
  u32 device_id{};
  u32 driver_version{};
  u32 vulkan_api_version{};
};

/** Owned, tightly packed RGBA8 pixels in top-to-bottom row order. */
struct RgbaFrame
{
  u32 width{};
  u32 height{};
  std::vector<std::uint8_t> pixels;
};

/** Configuration for a renderer and its optional quilt output. */
struct RendererConfig
{
  std::string ply_path;
  bool enable_validation = false;
  bool enable_imgui = true;
  bool enable_gpu_timers = false;
  bool capture_gpu_timing_samples = false;
  FrameSubmissionPolicy submission_policy = FrameSubmissionPolicy::kLatest;
  FrameRateConfig frame_rate{};
  /// Quilt dimensions as {columns, rows}; {1, 1} selects the mono path.
  std::array<u32, 2> lfd_grid{ 1U, 1U };
  /// Atlas width and height; {0,0} uses the selected output extent for each tile.
  std::array<u32, 2> quilt_size{ 0U, 0U };
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
  /** Render and capture exactly one camera frame in offscreen mode. */
  [[nodiscard]] auto render_frame(Camera const &camera) -> std::expected<RgbaFrame, Error>;
  [[nodiscard]] auto poll_error() const -> std::optional<Error>;
  [[nodiscard]] auto completed_frame_count() const -> u64;
  [[nodiscard]] auto recreated_frame_count() const -> u64;
  [[nodiscard]] auto device_info() const -> RenderDeviceInfo;
  /** Save the latest scene color target as PNG, excluding the ImGui overlay. */
  [[nodiscard]] auto save_frame_png(std::string_view path) -> std::expected<void, Error>;
  [[nodiscard]] auto wait_idle() -> std::expected<void, Error>;
  /**
   * Drain collected samples in frame-ID order and clear the collection.
   * This is thread-safe and does not synchronize the GPU; call wait_idle() first
   * for a complete batch. Returns an error when collection is disabled.
   */
  [[nodiscard]] auto take_gpu_timings() -> std::expected<std::vector<GpuFrameTimings>, Error>;
  [[nodiscard]] auto reserve_gpu_timing_samples(size_t count) -> std::expected<void, Error>;
  [[nodiscard]] auto gpu_timestamp_period_ns() const -> double;
  [[nodiscard]] auto gpu_timestamp_valid_bits() const -> u32;

  /** Render one quilt cell full-screen instead of the complete atlas. */
  [[nodiscard]] auto set_lfd_emulate(bool active) -> std::expected<void, Error>;
  [[nodiscard]] auto lfd_emulate_active() const noexcept -> bool;
  [[nodiscard]] auto set_lfd_emulate_cell(u32 col, u32 row) -> std::expected<void, Error>;
  [[nodiscard]] auto lfd_emulate_cell() const noexcept -> std::array<u32, 2>;
  [[nodiscard]] auto lfd_grid() const noexcept -> std::array<u32, 2>;

private:
  struct Impl;
  explicit Renderer(beman::indirect::indirect<Impl> impl);

  beman::indirect::indirect<Impl> impl_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_RENDERER_HPP
