#include "benchmark/fixture/manifest.hpp"

#include "benchmark/fixture/ply_writer.hpp"
#include "benchmark/fixture/profile.hpp"

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace vkgsplat::benchmark {
namespace {

  inline constexpr u32 kHexWidth = 16U;
  inline constexpr u32 kHexSeedWidth = 16U;
  inline constexpr u64 kMaximumManifestBytes = u64{ 16 } * 1024U;
  inline constexpr unsigned char kFirstPrintableCharacter = 0x20U;

  [[nodiscard]] auto ValidSceneFilename(std::filesystem::path const &path) -> bool
  {
    auto const name = path.filename().string();
    return !name.empty() && std::ranges::all_of(name, [](char character) -> bool {
      return character != '"' && character != '\\' && static_cast<unsigned char>(character) >= kFirstPrintableCharacter;
    });
  }

  [[nodiscard]] auto Hex(u64 value, u32 width) -> std::string
  {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "0x" << std::hex << std::setfill('0') << std::setw(static_cast<int>(width)) << value;
    return stream.str();
  }

  [[nodiscard]] auto ManifestText(std::filesystem::path const &ply_path, FixtureInfo const &info) -> std::string
  {
    auto const profile = BaselineProfile();
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "{\n"
           << R"(  "schema_version": )" << kManifestSchemaVersion << ",\n"
           << R"(  "profile": ")" << (info.canonical ? kBaselineId : kCustomId)
           << R"(",)"
              "\n"
           << R"(  "generator_version": )" << info.scene.generator_version << ",\n"
           << R"(  "distribution": ")" << kDistributionId
           << R"(",)"
              "\n"
           << R"(  "seed": ")" << Hex(info.scene.seed, kHexSeedWidth)
           << R"(",)"
              "\n"
           << R"(  "gaussians": )" << info.scene.gaussian_count << ",\n"
           << R"(  "scene_file": ")" << ply_path.filename().string()
           << R"(",)"
              "\n"
           << R"(  "scene_hash": ")" << Hex(info.hash, kHexWidth)
           << R"(",)"
              "\n"
           << R"(  "scene_bytes": )" << info.bytes << ",\n"
           << R"(  "camera": {)"
              "\n"
           << R"(    "position": [0.0, 0.0, )" << kCameraDistance << "],\n"
           << R"(    "target": [0.0, 0.0, 0.0],)"
              "\n"
           << R"(    "up": [0.0, 1.0, 0.0],)"
              "\n"
           << R"(    "fov_degrees": )" << profile.camera.fov_degrees << ",\n"
           << R"(    "near_plane": )" << profile.camera.near_plane << ",\n"
           << R"(    "far_plane": )" << profile.camera.far_plane << "\n"
           << "  },\n"
           << R"(  "rendering": {)"
              "\n"
           << R"(    "width": )" << profile.width << ",\n"
           << R"(    "height": )" << profile.height << ",\n"
           << R"(    "lfd_grid": [)" << kMonoGridSize << ", " << kMonoGridSize << "],\n"
           << R"(    "imgui": false,)"
              "\n"
           << R"(    "gpu_timers": false,)"
              "\n"
           << R"(    "validation": false,)"
              "\n"
           << R"(    "frame_pacing": "uncapped",)"
              "\n"
           << R"(    "sh_degree": )" << profile.sh_degree << "\n"
           << "  }\n"
           << "}\n";
    return stream.str();
  }

  [[nodiscard]] auto StringField(std::string_view text, char const *key) -> std::expected<std::string_view, Error>
  {
    auto const prefix = std::string{ '"' } + key + R"(": ")";
    auto const start = text.find(prefix);
    if (start == std::string_view::npos) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "missing manifest field") };
    }
    auto const value_start = start + prefix.size();
    auto const end = text.find('"', value_start);
    if (end == std::string_view::npos) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "unterminated manifest field") };
    }
    return text.substr(value_start, end - value_start);
  }

  [[nodiscard]] auto NumberField(std::string_view text, char const *key) -> std::expected<u64, Error>
  {
    auto const prefix = std::string{ '"' } + key + R"(": )";
    auto const start = text.find(prefix);
    if (start == std::string_view::npos) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "missing manifest number") };
    }
    std::istringstream stream{ std::string{ text.substr(start + prefix.size()) } };
    stream.imbue(std::locale::classic());
    u64 value{};
    if (!(stream >> value)) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "invalid manifest number") };
    }
    return value;
  }

  [[nodiscard]] auto HexField(std::string_view text, char const *key) -> std::expected<u64, Error>
  {
    auto value = StringField(text, key);
    if (!value || !value->starts_with("0x")) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "invalid manifest hexadecimal field") };
    }
    auto digits = *value;
    digits.remove_prefix(std::string_view{ "0x" }.size());
    std::istringstream stream{ std::string{ digits } };
    stream.imbue(std::locale::classic());
    u64 result{};
    if (!(stream >> std::hex >> result) || stream.peek() != std::char_traits<char>::eof()) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "invalid manifest hexadecimal value") };
    }
    return result;
  }

}// namespace

auto ManifestPath(std::filesystem::path const &ply_path) -> std::filesystem::path
{
  auto path = ply_path;
  path += ".json";
  return path;
}

auto WriteManifest(std::filesystem::path const &ply_path, FixtureInfo const &info) -> std::expected<void, Error>
{
  if (!ValidSceneFilename(ply_path) || info.canonical != IsCanonical(info.scene)) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "invalid fixture identity") };
  }
  if (info.canonical && info.hash != kBaselinePlyHash) {
    return std::unexpected{ MakeError(
      std::errc::invalid_argument, "canonical PLY checksum does not match baseline-v1") };
  }
  auto const path = ManifestPath(ply_path);
  auto const temporary = std::filesystem::path{ path.string() + ".tmp" };
  if (std::filesystem::exists(temporary)) {
    return std::unexpected{ MakeError(std::errc::file_exists, "temporary manifest exists") };
  }
  std::ofstream stream{ temporary, std::ios::binary | std::ios::trunc };
  if (!stream) { return std::unexpected{ MakeError(std::errc::io_error, "cannot open temporary manifest") }; }
  stream << ManifestText(ply_path, info);
  stream.close();
  if (!stream) {
    std::filesystem::remove(temporary);
    return std::unexpected{ MakeError(std::errc::io_error, "failed writing manifest") };
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary);
    return std::unexpected{ MakeError(error, "failed replacing manifest") };
  }
  return {};
}

auto VerifyFixture(std::filesystem::path const &ply_path) -> std::expected<FixtureInfo, Error>
{
  if (!ValidSceneFilename(ply_path)) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "invalid fixture filename") };
  }
  std::error_code error;
  auto const manifest_size = std::filesystem::file_size(ManifestPath(ply_path), error);
  if (error || manifest_size > kMaximumManifestBytes) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "missing or oversized manifest") };
  }
  std::ifstream stream{ ManifestPath(ply_path), std::ios::binary };
  std::string const text{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
  auto const profile = StringField(text, "profile");
  auto const scene_file = StringField(text, "scene_file");
  auto const count = NumberField(text, "gaussians");
  auto const version = NumberField(text, "generator_version");
  auto const schema = NumberField(text, "schema_version");
  auto const seed = HexField(text, "seed");
  auto const hash = HexField(text, "scene_hash");
  auto const bytes = NumberField(text, "scene_bytes");
  if (!profile || !scene_file || !count || !version || !schema || !seed || !hash || !bytes
      || *schema != kManifestSchemaVersion || *version != kGeneratorVersion || *count == 0U
      || *count > std::numeric_limits<u32>::max() || *scene_file != ply_path.filename().string()) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "invalid fixture manifest") };
  }
  FixtureInfo info{ .scene = { .gaussian_count = static_cast<u32>(*count),
                      .seed = *seed,
                      .generator_version = static_cast<u32>(*version) },
    .hash = *hash,
    .bytes = *bytes };
  info.canonical = IsCanonical(info.scene);
  if (*profile != (info.canonical ? kBaselineId : kCustomId) || text != ManifestText(ply_path, info)) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "manifest content does not match profile") };
  }
  auto const actual_bytes = std::filesystem::file_size(ply_path, error);
  if (error || actual_bytes != info.bytes) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "fixture size mismatch") };
  }
  auto actual_hash = FingerprintFile(ply_path);
  if (!actual_hash) { return std::unexpected{ std::move(actual_hash.error()) }; }
  if (*actual_hash != info.hash) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "fixture checksum mismatch") };
  }
  if (info.canonical && *actual_hash != kBaselinePlyHash) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "canonical PLY checksum mismatch") };
  }
  return info;
}

}// namespace vkgsplat::benchmark
