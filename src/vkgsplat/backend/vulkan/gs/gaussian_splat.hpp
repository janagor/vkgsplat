#ifndef VKGSPLAT_BACKEND_VULKAN_GS_GAUSSIAN_SPLAT_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_GAUSSIAN_SPLAT_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::gs {

// 3DGS SH degree 3: (degree + 1)^2 = 16 coeffs/channel × 3 channels = 48.
// Stored as DC (f_dc_0..2) plus higher-order rest (f_rest_0..44).
inline constexpr u32 kShDegree = 3;// max SH degree stored in appearance; runtime uses project_push.sh_degree
inline constexpr u32 kViewerShDegree = 3;// full view-dependent SH (0 = SH0 only, faster proj)
inline constexpr u32 kShDcCoeffs = 3;
inline constexpr u32 kShRestCoeffs = 45;
inline constexpr u32 kShTotalCoeffs = kShDcCoeffs + kShRestCoeffs;

// Y_0^0 normalization constant used by 3DGS SH0 → RGB.
inline constexpr f32 kShC0 = 0.28209479177387814F;

inline constexpr u32 kGeometryFloats = 11;
inline constexpr u32 kAppearanceFloats = kShTotalCoeffs;

// Geometry buffer element: pose, shape, and opacity (PLY: x/y/z, scale_*, rot_*, opacity).
// Packed AoS layout for a dedicated geometry storage buffer.
struct GaussianGeometry
{
  std::array<f32, 3> position{};
  std::array<f32, 3> scale{};// log-space scales (exp in shaders)
  // PLY order rot_0..3 → (w, x, y, z).
  std::array<f32, 4> rotation{ 1.0F, 0.0F, 0.0F, 0.0F };
  f32 opacity{};// logit opacity (sigmoid in shaders)
};

static_assert(sizeof(GaussianGeometry) == kGeometryFloats * sizeof(f32));
static_assert(alignof(GaussianGeometry) == alignof(f32));
static_assert(TriviallyCopyable<GaussianGeometry>);
static_assert(std::is_standard_layout_v<GaussianGeometry>);

// Appearance buffer element: view-dependent color via spherical harmonics.
// Packed AoS layout for a dedicated appearance storage buffer.
struct GaussianAppearance
{
  std::array<f32, kShDcCoeffs> f_dc{};// f_dc_0, f_dc_1, f_dc_2
  std::array<f32, kShRestCoeffs> f_rest{};// f_rest_0 .. f_rest_44
};

static_assert(sizeof(GaussianAppearance) == kAppearanceFloats * sizeof(f32));
static_assert(alignof(GaussianAppearance) == alignof(f32));
static_assert(TriviallyCopyable<GaussianAppearance>);
static_assert(std::is_standard_layout_v<GaussianAppearance>);

// Full per-splat record (CPU-side convenience; GPU keeps geometry/appearance split).
struct GaussianSplat
{
  GaussianGeometry geometry{};
  GaussianAppearance appearance{};
};

static_assert(sizeof(GaussianSplat) == sizeof(GaussianGeometry) + sizeof(GaussianAppearance));
static_assert(TriviallyCopyable<GaussianSplat>);
static_assert(std::is_standard_layout_v<GaussianSplat>);

// Stage 1 projection output (AoS). radius == 0 marks a culled splat.
// Layout: mean(2) + conic(3) + depth(1) + radius(1) + rgb(3) + opacity(1).
inline constexpr u32 kProjectedFloats = 11;

struct GaussianProjected
{
  std::array<f32, 2> screen_position{};// pixel-space mean
  std::array<f32, 3> conic{};// Σ₂D as (xx, xy, yy) for oriented-quad HW path
  f32 depth{};// view-space z
  f32 radius{};// screen-space extent in pixels
  std::array<f32, 3> color{};// view-dependent SH RGB (precomputed once per frame)
  f32 opacity{};// sigmoid(logit) opacity
};

static_assert(sizeof(GaussianProjected) == kProjectedFloats * sizeof(f32));
static_assert(alignof(GaussianProjected) == alignof(f32));
static_assert(TriviallyCopyable<GaussianProjected>);
static_assert(std::is_standard_layout_v<GaussianProjected>);

// Stage 2 tile binning.
inline constexpr u32 kTileSize = 16;
// Conservative upper bound on tiles touched per splat (16x16 tile grid).
inline constexpr u32 kMaxTilesPerSplat = 64;

// 32-bit sort key packed as (tile_id << 16) | (depth_bits >> 16).
// Fits tile grids up to 65536 and keeps coarse depth order within a tile.
struct BinningKey
{
  u32 packed{};
  u32 pad{};// keeps 8-byte stride used by existing KEY_STRIDE=2 shaders
};

auto constexpr kBinningKeySize = 8;
static_assert(sizeof(BinningKey) == kBinningKeySize);
static_assert(TriviallyCopyable<BinningKey>);
static_assert(std::is_standard_layout_v<BinningKey>);

// Stage 3: per-tile start/end into the sorted instance list.
struct TileRange
{
  u32 start{};// inclusive
  u32 end{};// exclusive; empty when start == end
};

auto constexpr kTileRangeSize = 8;
static_assert(sizeof(TileRange) == kTileRangeSize);
static_assert(TriviallyCopyable<TileRange>);
static_assert(std::is_standard_layout_v<TileRange>);

// Max tile grid supported for tile_ranges allocation (16px tiles → up to 4096² viewport).
inline constexpr u32 kMaxTileGridDim = 256;
inline constexpr u32 kMaxTiles = kMaxTileGridDim * kMaxTileGridDim;

[[nodiscard]] inline auto Sigmoid(f32 value) -> f32 { return 1.0F / (1.0F + std::exp(-value)); }

[[nodiscard]] inline auto Sh0ToRgb(std::array<f32, 3> const &f_dc) -> std::array<f32, 3>
{
  return {
    std::clamp(0.5F + (kShC0 * f_dc[0]), 0.0F, 1.0F),// NOLINT(readability-magic-numbers)
    std::clamp(0.5F + (kShC0 * f_dc[1]), 0.0F, 1.0F),// NOLINT(readability-magic-numbers)
    std::clamp(0.5F + (kShC0 * f_dc[2]), 0.0F, 1.0F),// NOLINT(readability-magic-numbers)
  };
}

[[nodiscard]] inline auto ScalesFromLog(std::array<f32, 3> const &log_scale) -> std::array<f32, 3>
{ return { std::exp(log_scale[0]), std::exp(log_scale[1]), std::exp(log_scale[2]) }; }

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_GAUSSIAN_SPLAT_HPP
