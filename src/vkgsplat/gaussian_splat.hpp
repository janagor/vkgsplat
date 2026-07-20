#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

#include <vkgsplat/types.hpp>

namespace vkgsplat {

// 3DGS SH degree 3: (degree + 1)^2 = 16 coeffs/channel × 3 channels = 48.
// Stored as DC (f_dc_0..2) plus higher-order rest (f_rest_0..44).
inline constexpr u32 k_sh_degree = 3;
inline constexpr u32 k_sh_dc_coeffs = 3;
inline constexpr u32 k_sh_rest_coeffs = 45;
inline constexpr u32 k_sh_total_coeffs = k_sh_dc_coeffs + k_sh_rest_coeffs;

// Geometry buffer element: pose, shape, and opacity (PLY: x/y/z, scale_*, rot_*, opacity).
// Packed AoS layout for a dedicated geometry storage buffer.
struct GaussianGeometry
{
  std::array<f32, 3> position{};
  std::array<f32, 3> scale{};
  // PLY order rot_0..3 → (w, x, y, z).
  std::array<f32, 4> rotation{ 1.0F, 0.0F, 0.0F, 0.0F };
  f32 opacity{};
};

static_assert(sizeof(GaussianGeometry) == 11U * sizeof(f32));
static_assert(alignof(GaussianGeometry) == alignof(f32));
static_assert(std::is_trivially_copyable_v<GaussianGeometry>);
static_assert(std::is_standard_layout_v<GaussianGeometry>);

// Appearance buffer element: view-dependent color via spherical harmonics.
// Packed AoS layout for a dedicated appearance storage buffer.
struct GaussianAppearance
{
  std::array<f32, k_sh_dc_coeffs> f_dc{};// f_dc_0, f_dc_1, f_dc_2
  std::array<f32, k_sh_rest_coeffs> f_rest{};// f_rest_0 .. f_rest_44
};

static_assert(sizeof(GaussianAppearance) == k_sh_total_coeffs * sizeof(f32));
static_assert(alignof(GaussianAppearance) == alignof(f32));
static_assert(std::is_trivially_copyable_v<GaussianAppearance>);
static_assert(std::is_standard_layout_v<GaussianAppearance>);

// Full per-splat record (CPU-side convenience; GPU keeps geometry/appearance split).
struct GaussianSplat
{
  GaussianGeometry geometry{};
  GaussianAppearance appearance{};
};

static_assert(sizeof(GaussianSplat) == sizeof(GaussianGeometry) + sizeof(GaussianAppearance));
static_assert(std::is_trivially_copyable_v<GaussianSplat>);
static_assert(std::is_standard_layout_v<GaussianSplat>);

}// namespace vkgsplat
