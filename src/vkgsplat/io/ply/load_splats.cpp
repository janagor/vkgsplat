#include "io/ply/load_splats.hpp"

#include "3dgs/gaussian_splat.hpp"
#include "io/ply/miniply.hpp"
#include <vkgsplat/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vkgsplat {

namespace {

struct ExtractedSplatAttributes
{
  std::vector<f32> positions;
  std::vector<f32> scales;
  std::vector<f32> rotations;
  std::vector<f32> opacities;
  std::vector<f32> f_dc;
  std::vector<f32> f_rest;
};

template<size_t N>
[[nodiscard]] auto make_indexed_names(char const *prefix) -> std::array<std::string, N>
{
  std::array<std::string, N> names{};
  for (size_t i = 0; i < N; ++i) { names.at(i) = std::string{ prefix } + std::to_string(i); }
  return names;
}

template<size_t N>
[[nodiscard]] auto names_as_c_strs(std::array<std::string, N> const &names) -> std::array<char const *, N>
{
  std::array<char const *, N> c_strs{};
  for (size_t i = 0; i < N; ++i) { c_strs.at(i) = names.at(i).c_str(); }
  return c_strs;
}

template<size_t N>
[[nodiscard]] auto require_properties(miniply::PLYReader &reader,
  std::array<uint32_t, N> &indexes,
  std::array<char const *, N> const &names,
  std::string_view missing_message) -> std::expected<void, std::string>
{
  if (!reader.find_properties(indexes, names)) { return std::unexpected{ std::string{ missing_message } }; }
  return {};
}

template<size_t N>
[[nodiscard]] auto extract_floats(miniply::PLYReader &reader,
  std::array<uint32_t, N> const &indexes,
  std::vector<f32> &destination,
  std::string_view failure_message) -> std::expected<void, std::string>
{
  if (!reader.extract_properties(indexes, miniply::PLYPropertyType::Float, destination.data())) {
    return std::unexpected{ std::string{ failure_message } };
  }
  return {};
}

[[nodiscard]] auto pack_splat(u32 splat_idx, ExtractedSplatAttributes const &attrs) -> GaussianSplat
{
  size_t const base3 = static_cast<size_t>(splat_idx) * 3U;
  size_t const base4 = static_cast<size_t>(splat_idx) * 4U;
  size_t const rest_base = static_cast<size_t>(splat_idx) * k_sh_rest_coeffs;

  GaussianSplat splat{};
  splat.geometry.position = {
    attrs.positions.at(base3), attrs.positions.at(base3 + 1U), attrs.positions.at(base3 + 2U)
  };
  splat.geometry.scale = {
    attrs.scales.at(base3), attrs.scales.at(base3 + 1U), attrs.scales.at(base3 + 2U)
  };
  splat.geometry.rotation = { attrs.rotations.at(base4),
    attrs.rotations.at(base4 + 1U),
    attrs.rotations.at(base4 + 2U),
    attrs.rotations.at(base4 + 3U) };
  splat.geometry.opacity = attrs.opacities.at(splat_idx);

  splat.appearance.f_dc = { attrs.f_dc.at(base3), attrs.f_dc.at(base3 + 1U), attrs.f_dc.at(base3 + 2U) };
  for (u32 rest_idx = 0; rest_idx < k_sh_rest_coeffs; ++rest_idx) {
    splat.appearance.f_rest.at(rest_idx) = attrs.f_rest.at(rest_base + rest_idx);
  }
  return splat;
}

[[nodiscard]] auto load_vertex_splats(miniply::PLYReader &reader, u32 count)
  -> std::expected<SplatCpuData, std::string>
{
  u32 const available = reader.num_rows();
  if (available == 0) { return std::unexpected{ "PLY vertex element is empty" }; }

  u32 const splat_count = std::min(count, available);

  std::array<uint32_t, 3> position_idx{};
  std::array<uint32_t, 3> scale_idx{};
  std::array<uint32_t, 4> rotation_idx{};
  std::array<uint32_t, 1> opacity_idx{};
  std::array<uint32_t, k_sh_dc_coeffs> f_dc_idx{};
  std::array<uint32_t, k_sh_rest_coeffs> f_rest_idx{};

  static constexpr std::array<char const *, 3> position_names{ "x", "y", "z" };
  static constexpr std::array<char const *, 3> scale_names{ "scale_0", "scale_1", "scale_2" };
  static constexpr std::array<char const *, 4> rotation_names{ "rot_0", "rot_1", "rot_2", "rot_3" };
  static constexpr std::array<char const *, 1> opacity_names{ "opacity" };
  static constexpr std::array<char const *, 3> f_dc_names{ "f_dc_0", "f_dc_1", "f_dc_2" };
  auto const f_rest_name_storage = make_indexed_names<k_sh_rest_coeffs>("f_rest_");
  auto const f_rest_names = names_as_c_strs(f_rest_name_storage);

  if (auto result = require_properties(reader, position_idx, position_names, "PLY missing x/y/z"); !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = require_properties(reader, scale_idx, scale_names, "PLY missing scale_0/1/2"); !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = require_properties(reader, rotation_idx, rotation_names, "PLY missing rot_0/1/2/3");
      !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = require_properties(reader, opacity_idx, opacity_names, "PLY missing opacity"); !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = require_properties(reader, f_dc_idx, f_dc_names, "PLY missing f_dc_0/1/2"); !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = require_properties(reader, f_rest_idx, f_rest_names, "PLY missing f_rest_0..44");
      !result) {
    return std::unexpected{ std::move(result.error()) };
  }

  ExtractedSplatAttributes attrs{
    .positions = std::vector<f32>(static_cast<size_t>(available) * 3U),
    .scales = std::vector<f32>(static_cast<size_t>(available) * 3U),
    .rotations = std::vector<f32>(static_cast<size_t>(available) * 4U),
    .opacities = std::vector<f32>(static_cast<size_t>(available)),
    .f_dc = std::vector<f32>(static_cast<size_t>(available) * k_sh_dc_coeffs),
    .f_rest = std::vector<f32>(static_cast<size_t>(available) * k_sh_rest_coeffs),
  };

  if (auto result = extract_floats(reader, position_idx, attrs.positions, "failed to extract positions");
      !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = extract_floats(reader, scale_idx, attrs.scales, "failed to extract scales"); !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = extract_floats(reader, rotation_idx, attrs.rotations, "failed to extract rotations");
      !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = extract_floats(reader, opacity_idx, attrs.opacities, "failed to extract opacity");
      !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = extract_floats(reader, f_dc_idx, attrs.f_dc, "failed to extract f_dc"); !result) {
    return std::unexpected{ std::move(result.error()) };
  }
  if (auto result = extract_floats(reader, f_rest_idx, attrs.f_rest, "failed to extract f_rest"); !result) {
    return std::unexpected{ std::move(result.error()) };
  }

  SplatCpuData splats{};
  splats.geometries.resize(splat_count);
  splats.appearances.resize(splat_count);

  for (u32 splat_idx = 0; splat_idx < splat_count; ++splat_idx) {
    auto const packed = pack_splat(splat_idx, attrs);
    splats.geometries.at(splat_idx) = packed.geometry;
    splats.appearances.at(splat_idx) = packed.appearance;
  }

  return splats;
}

}// namespace

auto load_splats_from_ply(std::string_view ply_path, u32 count) -> std::expected<SplatCpuData, std::string>
{
  if (count == 0) { return std::unexpected{ "splat count must be greater than zero" }; }

  std::string const path{ ply_path };
  miniply::PLYReader reader(path.c_str());
  if (!reader.valid()) { return std::unexpected{ "failed to open or parse PLY header: " + path }; }

  while (reader.has_element()) {
    if (reader.element()->name != miniply::kPLYVertexElement) {
      reader.next_element();
      continue;
    }

    if (!reader.load_element()) { return std::unexpected{ "failed to load vertex element from PLY" }; }
    return load_vertex_splats(reader, count);
  }

  return std::unexpected{ "PLY file does not contain a vertex element" };
}

}// namespace vkgsplat
