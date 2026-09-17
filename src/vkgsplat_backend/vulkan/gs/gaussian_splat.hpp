#ifndef VKGSPLAT_BACKEND_VULKAN_GS_GAUSSIAN_SPLAT_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_GAUSSIAN_SPLAT_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::gs {

// Stage 1 projection output (AoS). radius == 0 marks a culled splat.
// Layout: mean(2) + conic(3) + depth(1) + radius(1) + rgb(3) + opacity(1).
inline constexpr u32 kProjectedFloats = 11;

struct GaussianProjected
{
  std::array<f32, 2> screen_position{};// pixel-space mean
  std::array<f32, 3> conic{};// inverse Σ₂D as (xx, xy, yy) for EWA fragment eval
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
// Default NDC xy cull half-extent for projection (matches classic 3DGS fringe).
inline constexpr f32 kDefaultProjectionCullMargin = 1.3F;
// Conservative upper bound on tiles touched per splat (16x16 tile grid).
inline constexpr u32 kMaxTilesPerSplat = 64;
// Radix sort: elements processed per workgroup invocation.
inline constexpr u32 kRadixBlocksPerWorkgroup = 32;

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

// Legacy host/device sort record (luminance key + splat index).
struct SortEntry
{
  f32 luminance{};
  u32 index{};
};

inline constexpr size_t kSortEntrySize = sizeof(f32) + sizeof(u32);
static_assert(sizeof(SortEntry) == kSortEntrySize);
static_assert(TriviallyCopyable<SortEntry>);
static_assert(std::is_standard_layout_v<SortEntry>);

// Max tile grid supported for tile_ranges allocation (16px tiles → up to 4096² viewport).
inline constexpr u32 kMaxTileGridDim = 256;
inline constexpr u32 kMaxTiles = kMaxTileGridDim * kMaxTileGridDim;

[[nodiscard]] inline auto Sigmoid(f32 value) -> f32 { return 1.0F / (1.0F + std::exp(-value)); }

[[nodiscard]] inline auto Sh0ToRgb(std::array<f32, 3> const &f_dc) -> std::array<f32, 3>
{
  // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
  return {
    std::clamp(0.5F + (kShC0 * f_dc.at(0)), 0.0F, 1.0F),
    std::clamp(0.5F + (kShC0 * f_dc.at(1)), 0.0F, 1.0F),
    std::clamp(0.5F + (kShC0 * f_dc.at(2)), 0.0F, 1.0F),
  };
  // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
}

[[nodiscard]] inline auto ScalesFromLog(std::array<f32, 3> const &log_scale) -> std::array<f32, 3>
{ return { std::exp(log_scale.at(0)), std::exp(log_scale.at(1)), std::exp(log_scale.at(2)) }; }

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_GAUSSIAN_SPLAT_HPP
