#include "benchmark/fixture/ply_writer.hpp"

#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <numeric>
#include <ostream>
#include <string>
#include <system_error>

namespace vkgsplat::benchmark {
namespace {

  inline constexpr u64 kFnvOffset = 0xCBF29CE484222325ULL;
  inline constexpr u64 kFnvPrime = 0x100000001B3ULL;
  inline constexpr u32 kByteMask = 0xFFU;
  inline constexpr u32 kBitsPerByte = 8U;
  inline constexpr size_t kHashBufferSize = size_t{ 64 } * 1024U;
  inline constexpr f32 kQuaternionNormTolerance = 0.0001F;
  inline constexpr u32 kVectorComponents = 3U;
  inline constexpr u32 kQuaternionComponents = 4U;

  template<size_t N> [[nodiscard]] auto FiniteValues(std::array<f32, N> const &values) -> bool
  {
    return std::ranges::all_of(values, [](f32 value) -> bool { return std::isfinite(value); });
  }

  [[nodiscard]] auto ValidGeometry(gs::GaussianGeometry const &geometry) -> bool
  {
    if (!FiniteValues(geometry.position) || !FiniteValues(geometry.scale) || !FiniteValues(geometry.rotation)
        || !std::isfinite(geometry.opacity)) {
      return false;
    }
    auto const norm =
      std::inner_product(geometry.rotation.begin(), geometry.rotation.end(), geometry.rotation.begin(), f32{ 0.0F });
    return std::abs(norm - 1.0F) <= kQuaternionNormTolerance;
  }

  [[nodiscard]] auto ValidAppearance(gs::GaussianAppearance const &appearance) -> bool
  { return FiniteValues(appearance.f_dc) && FiniteValues(appearance.f_rest); }

  [[nodiscard]] auto ValidScene(SplatCpuData const &scene) -> bool
  {
    if (scene.geometries.empty() || scene.geometries.size() != scene.appearances.size()) { return false; }
    for (size_t index = 0; index < scene.geometries.size(); ++index) {
      if (!ValidGeometry(scene.geometries.at(index)) || !ValidAppearance(scene.appearances.at(index))) { return false; }
    }
    return true;
  }

  auto WriteFloat(std::ostream &stream, f32 value) -> void
  {
    auto const bits = std::bit_cast<u32>(value);
    for (u32 byte = 0; byte < sizeof(f32); ++byte) {
      stream.put(static_cast<char>((bits >> (byte * kBitsPerByte)) & kByteMask));
    }
  }

  auto WriteHeader(std::ostream &stream, size_t count) -> void
  {
    stream << "ply\nformat binary_little_endian 1.0\nelement vertex " << count << '\n';
    for (auto const *name : { "x", "y", "z", "nx", "ny", "nz" }) { stream << "property float " << name << '\n'; }
    for (u32 index = 0; index < gs::kShDcCoeffs; ++index) { stream << "property float f_dc_" << index << '\n'; }
    for (u32 index = 0; index < gs::kShRestCoeffs; ++index) { stream << "property float f_rest_" << index << '\n'; }
    stream << "property float opacity\n";
    for (u32 index = 0; index < kVectorComponents; ++index) { stream << "property float scale_" << index << '\n'; }
    for (u32 index = 0; index < kQuaternionComponents; ++index) { stream << "property float rot_" << index << '\n'; }
    stream << "end_header\n";
  }

}// namespace

auto WritePly(std::filesystem::path const &path, SplatCpuData const &scene) -> std::expected<void, Error>
{
  if (path.empty() || path.filename().empty() || !ValidScene(scene)) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "invalid PLY path or scene") };
  }
  auto const temporary = std::filesystem::path{ path.string() + ".tmp" };
  if (std::filesystem::exists(temporary)) {
    return std::unexpected{ MakeError(std::errc::file_exists, "temporary PLY path already exists") };
  }
  std::ofstream stream{ temporary, std::ios::binary | std::ios::trunc };
  if (!stream) { return std::unexpected{ MakeError(std::errc::io_error, "cannot open temporary PLY") }; }
  WriteHeader(stream, scene.geometries.size());
  for (size_t index = 0; index < scene.geometries.size(); ++index) {
    auto const &geometry = scene.geometries.at(index);
    auto const &appearance = scene.appearances.at(index);
    for (auto value : geometry.position) { WriteFloat(stream, value); }
    for (u32 normal = 0; normal < kVectorComponents; ++normal) { WriteFloat(stream, 0.0F); }
    for (auto value : appearance.f_dc) { WriteFloat(stream, value); }
    for (auto value : appearance.f_rest) { WriteFloat(stream, value); }
    WriteFloat(stream, geometry.opacity);
    for (auto value : geometry.scale) { WriteFloat(stream, value); }
    for (auto value : geometry.rotation) { WriteFloat(stream, value); }
    if (!stream) {
      stream.close();
      std::filesystem::remove(temporary);
      return std::unexpected{ MakeError(std::errc::io_error, "failed writing PLY records") };
    }
  }
  stream.close();
  if (!stream) {
    std::filesystem::remove(temporary);
    return std::unexpected{ MakeError(std::errc::io_error, "failed closing PLY") };
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary);
    return std::unexpected{ MakeError(error, "failed to replace PLY") };
  }
  return {};
}

auto FingerprintFile(std::filesystem::path const &path) -> std::expected<u64, Error>
{
  std::ifstream stream{ path, std::ios::binary };
  if (!stream) { return std::unexpected{ MakeError(std::errc::no_such_file_or_directory, "cannot open PLY") }; }
  std::array<char, kHashBufferSize> bytes{};
  u64 hash = kFnvOffset;
  while (stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) || stream.gcount() > 0) {
    for (std::streamsize index = 0; index < stream.gcount(); ++index) {
      hash ^= static_cast<u8>(bytes.at(static_cast<size_t>(index)));
      hash *= kFnvPrime;
    }
  }
  if (stream.bad()) { return std::unexpected{ MakeError(std::errc::io_error, "failed reading PLY") }; }
  return hash;
}

}// namespace vkgsplat::benchmark
