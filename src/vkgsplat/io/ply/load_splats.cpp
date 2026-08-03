#include "io/ply/load_splats.hpp"

#include "gs/gaussian_splat.hpp"
#include "io/ply/miniply.hpp"
#include <vkgsplat/error.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vkgsplat {

namespace {

  using gs::GaussianSplat;
  using gs::kShDcCoeffs;
  using gs::kShRestCoeffs;

  struct ExtractedSplatAttributes
  {
    std::vector<f32> positions;
    std::vector<f32> scales;
    std::vector<f32> rotations;
    std::vector<f32> opacities;
    std::vector<f32> f_dc;
    std::vector<f32> f_rest;
  };

  template<size_t N> [[nodiscard]] auto MakeIndexedNames(char const *prefix) -> std::array<std::string, N>
  {
    std::array<std::string, N> names{};
    std::ranges::copy(std::views::iota(size_t{ 0 }, N) | std::views::transform([prefix](size_t index) -> auto {
      return std::string{ prefix } + std::to_string(index);
    }),
      names.begin());
    return names;
  }

  template<size_t N>
  [[nodiscard]] auto NamesAsCStrs(std::array<std::string, N> const &names) -> std::array<char const *, N>
  {
    std::array<char const *, N> c_strs{};
    std::ranges::transform(names, c_strs.begin(), [](std::string const &name) -> auto { return name.c_str(); });
    return c_strs;
  }

  template<size_t N>
  [[nodiscard]] auto RequireProperties(miniply::PLYReader &reader,
    std::array<uint32_t, N> &indexes,
    std::array<char const *, N> const &names,
    std::string_view missing_message) -> std::expected<void, Error>
  {
    if (!reader.find_properties(indexes, names)) {
      return std::unexpected{ MakeError(ErrorIO::kMissingProperty, std::string{ missing_message }) };
    }
    return {};
  }

  template<size_t N>
  [[nodiscard]] auto ExtractFloats(miniply::PLYReader &reader,
    std::array<uint32_t, N> const &indexes,
    std::vector<f32> &destination,
    std::string_view failure_message) -> std::expected<void, Error>
  {
    if (!reader.extract_properties(indexes, miniply::PLYPropertyType::kFloat, destination.data())) {
      return std::unexpected{ MakeError(ErrorIO::kExtractPropertyFailed, std::string{ failure_message }) };
    }
    return {};
  }


  [[nodiscard]] auto PackSplat(u32 splat_idx, ExtractedSplatAttributes const &attrs) -> GaussianSplat
  {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    auto const idx = static_cast<std::ptrdiff_t>(splat_idx);

    // Note: we also drop the 'U' suffix on the multipliers so they default to signed integers
    auto const pos_it = attrs.positions.begin() + (idx * 3);
    auto const scl_it = attrs.scales.begin() + (idx * 3);
    auto const rot_it = attrs.rotations.begin() + (idx * 4);
    auto const dc_it = attrs.f_dc.begin() + (idx * 3);
    auto const rest_it = attrs.f_rest.begin() + (idx * static_cast<std::ptrdiff_t>(kShRestCoeffs));

    GaussianSplat splat{};

    splat.geometry.position = { pos_it[0], pos_it[1], pos_it[2] };
    splat.geometry.scale = { scl_it[0], scl_it[1], scl_it[2] };
    splat.geometry.rotation = { rot_it[0], rot_it[1], rot_it[2], rot_it[3] };

    // operator[] expects size_t (unsigned), so we cast it back or use splat_idx directly
    splat.geometry.opacity = attrs.opacities[static_cast<size_t>(splat_idx)];

    splat.appearance.f_dc = { dc_it[0], dc_it[1], dc_it[2] };

    std::copy_n(rest_it, kShRestCoeffs, std::begin(splat.appearance.f_rest));

    return splat;
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  }

  [[nodiscard]] auto LoadVertexSplats(miniply::PLYReader &reader, u32 count) -> std::expected<SplatCpuData, Error>
  {
    u32 const available = reader.num_rows();
    if (available == 0) {
      return std::unexpected{ MakeError(ErrorIO::kEmptyVertexElement, "PLY vertex element is empty") };
    }

    u32 const splat_count = std::min(count, available);

    std::array<uint32_t, 3> position_idx{};
    std::array<uint32_t, 3> scale_idx{};
    std::array<uint32_t, 4> rotation_idx{};
    std::array<uint32_t, 1> opacity_idx{};
    std::array<uint32_t, kShDcCoeffs> f_dc_idx{};
    std::array<uint32_t, kShRestCoeffs> f_rest_idx{};

    static constexpr std::array<char const *, 3> kPositionNames{ "x", "y", "z" };
    static constexpr std::array<char const *, 3> kScaleNames{ "scale_0", "scale_1", "scale_2" };
    static constexpr std::array<char const *, 4> kRotationNames{ "rot_0", "rot_1", "rot_2", "rot_3" };
    static constexpr std::array<char const *, 1> kOpacityNames{ "opacity" };
    static constexpr std::array<char const *, 3> kFDcNames{ "f_dc_0", "f_dc_1", "f_dc_2" };
    auto const f_rest_name_storage = MakeIndexedNames<kShRestCoeffs>("f_rest_");
    auto const f_rest_names = NamesAsCStrs(f_rest_name_storage);

    if (auto result = RequireProperties(reader, position_idx, kPositionNames, "PLY missing x/y/z"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = RequireProperties(reader, scale_idx, kScaleNames, "PLY missing scale_0/1/2"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = RequireProperties(reader, rotation_idx, kRotationNames, "PLY missing rot_0/1/2/3"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = RequireProperties(reader, opacity_idx, kOpacityNames, "PLY missing opacity"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = RequireProperties(reader, f_dc_idx, kFDcNames, "PLY missing f_dc_0/1/2"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = RequireProperties(reader, f_rest_idx, f_rest_names, "PLY missing f_rest_0..44"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }

    ExtractedSplatAttributes attrs{
      .positions = std::vector<f32>(static_cast<size_t>(available) * 3U),
      .scales = std::vector<f32>(static_cast<size_t>(available) * 3U),
      .rotations = std::vector<f32>(static_cast<size_t>(available) * 4U),
      .opacities = std::vector<f32>(static_cast<size_t>(available)),
      .f_dc = std::vector<f32>(static_cast<size_t>(available) * kShDcCoeffs),
      .f_rest = std::vector<f32>(static_cast<size_t>(available) * kShRestCoeffs),
    };

    if (auto result = ExtractFloats(reader, position_idx, attrs.positions, "failed to extract positions"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = ExtractFloats(reader, scale_idx, attrs.scales, "failed to extract scales"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = ExtractFloats(reader, rotation_idx, attrs.rotations, "failed to extract rotations"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = ExtractFloats(reader, opacity_idx, attrs.opacities, "failed to extract opacity"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = ExtractFloats(reader, f_dc_idx, attrs.f_dc, "failed to extract f_dc"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }
    if (auto result = ExtractFloats(reader, f_rest_idx, attrs.f_rest, "failed to extract f_rest"); !result) {
      return std::unexpected{ std::move(result.error()) };
    }

    SplatCpuData splats{};
    splats.geometries.resize(splat_count);
    splats.appearances.resize(splat_count);

    for (u32 splat_idx = 0; splat_idx < splat_count; ++splat_idx) {
      auto const packed = PackSplat(splat_idx, attrs);
      splats.geometries.at(splat_idx) = packed.geometry;
      splats.appearances.at(splat_idx) = packed.appearance;
    }

    return splats;
  }

}// namespace

auto LoadSplatsFromPly(std::string_view ply_path, u32 count) -> std::expected<SplatCpuData, Error>
{
  if (count == 0) {
    return std::unexpected{ MakeError(ErrorIO::kInvalidSplatCount, "splat count must be greater than zero") };
  }

  std::string const path{ ply_path };
  miniply::PLYReader reader(path.c_str());
  if (!reader.valid()) {
    return std::unexpected{ MakeError(ErrorIO::kFailedOpen, "failed to open or parse PLY header: " + path) };
  }

  while (reader.has_element()) {
    if (reader.element()->name != miniply::kPLYVertexElement) {
      reader.next_element();
      continue;
    }

    if (!reader.load_element()) {
      return std::unexpected{ MakeError(ErrorIO::kLoadElementFailed, "failed to load vertex element from PLY") };
    }
    return LoadVertexSplats(reader, count);
  }

  return std::unexpected{ MakeError(ErrorIO::kMissingVertexElement, "PLY file does not contain a vertex element") };
}

}// namespace vkgsplat
