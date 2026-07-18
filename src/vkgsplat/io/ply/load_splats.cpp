#include "io/ply/load_splats.hpp"

#include "io/ply/miniply.hpp"
#include "types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace vkgsplat {

namespace {

constexpr f32 k_sh_c0 = 0.28209479177387814F;

[[nodiscard]] auto f_dc_to_grayscale(f32 f_dc_0, f32 f_dc_1, f32 f_dc_2) -> f32
{
  f32 const red = 0.5F + (k_sh_c0 * f_dc_0);
  f32 const green = 0.5F + (k_sh_c0 * f_dc_1);
  f32 const blue = 0.5F + (k_sh_c0 * f_dc_2);
  f32 const gray = (0.299F * red) + (0.587F * green) + (0.114F * blue);
  return std::clamp(gray, 0.0F, 1.0F);
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

    u32 const available = reader.num_rows();
    if (available == 0) { return std::unexpected{ "PLY vertex element is empty" }; }

    u32 const splat_count = std::min(count, available);

    std::array<uint32_t, 3> position_idx{};
    std::array<uint32_t, 3> f_dc_idx{};
    static constexpr std::array<const char *, 3> position_names{ "x", "y", "z" };
    static constexpr std::array<const char *, 3> f_dc_names{ "f_dc_0", "f_dc_1", "f_dc_2" };

    if (!reader.find_properties(position_idx, position_names)) {
      return std::unexpected{ "PLY vertex element is missing x/y/z properties" };
    }
    if (!reader.find_properties(f_dc_idx, f_dc_names)) {
      return std::unexpected{ "PLY vertex element is missing f_dc_0/f_dc_1/f_dc_2 properties" };
    }

    std::vector<f32> positions(static_cast<size_t>(available) * 3U);
    std::vector<f32> f_dc(static_cast<size_t>(available) * 3U);
    if (!reader.extract_properties(position_idx, miniply::PLYPropertyType::Float, positions.data())) {
      return std::unexpected{ "failed to extract vertex positions from PLY" };
    }
    if (!reader.extract_properties(f_dc_idx, miniply::PLYPropertyType::Float, f_dc.data())) {
      return std::unexpected{ "failed to extract f_dc coefficients from PLY" };
    }

    SplatCpuData splats{};
    splats.positions.reserve(splat_count);
    splats.gray_colors.reserve(splat_count);

    for (u32 splat_idx = 0; splat_idx < splat_count; ++splat_idx) {
      size_t const base = static_cast<size_t>(splat_idx) * 3U;
      splats.positions.push_back(
        { positions.at(base), positions.at(base + 1U), positions.at(base + 2U) });
      splats.gray_colors.push_back(
        f_dc_to_grayscale(f_dc.at(base), f_dc.at(base + 1U), f_dc.at(base + 2U)));
    }

    return splats;
  }

  return std::unexpected{ "PLY file does not contain a vertex element" };
}

}// namespace vkgsplat
