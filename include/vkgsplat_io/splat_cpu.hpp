#ifndef VKGSPLAT_IO_SPLAT_CPU_HPP
#define VKGSPLAT_IO_SPLAT_CPU_HPP

#include <array>
#include <cstddef>
#include <type_traits>
#include <vector>

#include <vkgsplat_utility/concepts.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::gs {

// 3DGS SH degree 3: (degree + 1)^2 = 16 coeffs/channel x 3 channels = 48.
// Stored as DC (f_dc_0..2) plus higher-order rest (f_rest_0..44).
inline constexpr u32 kShDegree = 3;// max SH degree stored in appearance; runtime uses project_push.sh_degree
inline constexpr u32 kViewerShDegree = 3;// full view-dependent SH (0 = SH0 only, faster proj)
inline constexpr u32 kShDcCoeffs = 3;
inline constexpr u32 kShRestCoeffs = 45;
inline constexpr u32 kShTotalCoeffs = kShDcCoeffs + kShRestCoeffs;

// Y_0^0 normalization constant used by 3DGS SH0 -> RGB.
inline constexpr f32 kShC0 = 0.28209479177387814F;

inline constexpr u32 kGeometryFloats = 11;
inline constexpr u32 kAppearanceFloats = kShTotalCoeffs;

// Geometry buffer element: pose, shape, and opacity (PLY: x/y/z, scale_*, rot_*, opacity).
// Packed AoS layout for a dedicated geometry storage buffer.
struct GaussianGeometry
{
  std::array<f32, 3> position{};
  std::array<f32, 3> scale{};// log-space scales (exp in shaders)
  // PLY order rot_0..3 -> (w, x, y, z).
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

}// namespace vkgsplat::gs

namespace vkgsplat {

struct SplatCpuData
{
  std::vector<gs::GaussianGeometry> geometries;
  std::vector<gs::GaussianAppearance> appearances;
};

}// namespace vkgsplat

#endif// VKGSPLAT_IO_SPLAT_CPU_HPP
