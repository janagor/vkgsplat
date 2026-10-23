// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include "benchmark/fixture/manifest.hpp"
#include "benchmark/fixture/ply_writer.hpp"
#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/scene.hpp"

#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_io/load_splats.hpp>
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>

namespace {

inline constexpr vkgsplat::u32 kTestCount = 128U;
inline constexpr vkgsplat::u64 kGoldenHash = 0x6C7D8773EADAB651ULL;
inline constexpr vkgsplat::u64 kAlternateSeed = 0x12345678ULL;
inline constexpr vkgsplat::u32 kSmallCount = 7U;
inline constexpr vkgsplat::u32 kMiddleSampleIndex = 65'535U;
inline constexpr vkgsplat::u32 kLastSampleIndex = vkgsplat::benchmark::kBaselineCount - 1U;
inline constexpr vkgsplat::f32 kQuaternionTolerance = 0.0001F;
inline constexpr vkgsplat::f32 kMiddleSampleX = 1.386474609375F;
inline constexpr vkgsplat::f32 kLastSampleY = 1.221923828125F;
inline constexpr vkgsplat::f32 kLastSampleRest = 0.01171875F;
inline constexpr size_t kXAxis = 0U;
inline constexpr size_t kYAxis = 1U;

struct TempFixture
{
  std::filesystem::path directory;
  bool owns_directory;

  explicit TempFixture(std::string_view name)
    : directory(std::filesystem::temp_directory_path()
                / ("vkgsplat-fixture-" + std::string{ name } + "-"
                   + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
      owns_directory(std::filesystem::create_directory(directory))
  {}
  ~TempFixture()
  {
    if (owns_directory) { std::filesystem::remove_all(directory); }
  }
  TempFixture(TempFixture const &) = delete;
  auto operator=(TempFixture const &) -> TempFixture & = delete;
  TempFixture(TempFixture &&) = delete;
  auto operator=(TempFixture &&) -> TempFixture & = delete;
};

[[nodiscard]] auto ReadBytes(std::filesystem::path const &path) -> std::string
{
  std::ifstream stream{ path, std::ios::binary };
  return { std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] auto GeometryMatches(vkgsplat::gs::GaussianGeometry const &original,
  vkgsplat::gs::GaussianGeometry const &loaded) -> bool
{
  return original.position == loaded.position && original.scale == loaded.scale && original.rotation == loaded.rotation
         && original.opacity == loaded.opacity;
}

[[nodiscard]] auto AppearanceMatches(vkgsplat::gs::GaussianAppearance const &original,
  vkgsplat::gs::GaussianAppearance const &loaded) -> bool
{ return original.f_dc == loaded.f_dc && original.f_rest == loaded.f_rest; }

[[nodiscard]] auto RotationIsNormalized(vkgsplat::gs::GaussianGeometry const &geometry) -> bool
{
  auto const norm = std::inner_product(
    geometry.rotation.begin(), geometry.rotation.end(), geometry.rotation.begin(), vkgsplat::f32{ 0.0F });
  return std::abs(norm - 1.0F) < kQuaternionTolerance;
}

[[nodiscard]] auto
  SplatMatches(vkgsplat::SplatCpuData const &original, vkgsplat::SplatCpuData const &loaded, size_t index) -> bool
{
  return GeometryMatches(original.geometries.at(index), loaded.geometries.at(index))
         && AppearanceMatches(original.appearances.at(index), loaded.appearances.at(index))
         && RotationIsNormalized(original.geometries.at(index));
}

auto CheckRoundTrip(vkgsplat::SplatCpuData const &original, vkgsplat::SplatCpuData const &loaded) -> void
{
  REQUIRE(loaded.geometries.size() == kSmallCount);
  REQUIRE(loaded.appearances.size() == kSmallCount);
  for (size_t index = 0; index < kSmallCount; ++index) { REQUIRE(SplatMatches(original, loaded, index)); }
}

auto CheckPlyLayout(std::filesystem::path const &path) -> void
{
  auto const bytes = ReadBytes(path);
  auto const header_end = bytes.find("end_header\n");
  REQUIRE(header_end != std::string::npos);
  auto const record_bytes =
    static_cast<size_t>(kSmallCount) * static_cast<size_t>(vkgsplat::benchmark::kPlyBytesPerVertex);
  REQUIRE(bytes.size() == header_end + std::string_view{ "end_header\n" }.size() + record_bytes);
  REQUIRE(bytes.contains("format binary_little_endian 1.0\n"));
  REQUIRE(bytes.contains("property float f_rest_44\nproperty float opacity\n"));
}

}// namespace

TEST_CASE("fixture generation is reproducible and seed sensitive", "[fixture]")
{
  TempFixture const fixture{ "reproducibility" };
  vkgsplat::benchmark::BenchmarkSceneSpec const spec{ .gaussian_count = kTestCount };
  auto first = vkgsplat::benchmark::GenerateScene(spec);
  auto second = vkgsplat::benchmark::GenerateScene(spec);
  REQUIRE(first);
  REQUIRE(second);
  auto const path_a = fixture.directory / "a.ply";
  auto const path_b = fixture.directory / "b.ply";
  REQUIRE(vkgsplat::benchmark::WritePly(path_a, *first));
  REQUIRE(vkgsplat::benchmark::WritePly(path_b, *second));
  REQUIRE(ReadBytes(path_a) == ReadBytes(path_b));
  auto golden_hash = vkgsplat::benchmark::FingerprintFile(path_a);
  REQUIRE(golden_hash);
  REQUIRE(*golden_hash == kGoldenHash);
  auto different = vkgsplat::benchmark::GenerateScene({ .gaussian_count = kTestCount, .seed = kAlternateSeed });
  REQUIRE(different);
  REQUIRE(vkgsplat::benchmark::WritePly(path_b, *different));
  REQUIRE(*vkgsplat::benchmark::FingerprintFile(path_a) != *vkgsplat::benchmark::FingerprintFile(path_b));
}

TEST_CASE("generated PLY round trips through the production loader", "[fixture]")
{
  TempFixture const fixture{ "roundtrip" };
  auto scene = vkgsplat::benchmark::GenerateScene({ .gaussian_count = kSmallCount });
  REQUIRE(scene);
  auto const path = fixture.directory / "scene.ply";
  REQUIRE(vkgsplat::benchmark::WritePly(path, *scene));
  auto loaded = vkgsplat::LoadSplatsFromPly(path.string());
  REQUIRE(loaded);
  CheckRoundTrip(*scene, *loaded);
  CheckPlyLayout(path);
}

TEST_CASE("manifest verification detects file and metadata corruption", "[fixture]")
{
  TempFixture const fixture{ "manifest" };
  auto const path = fixture.directory / "custom.ply";
  vkgsplat::benchmark::BenchmarkSceneSpec const spec{ .gaussian_count = kSmallCount };
  auto scene = vkgsplat::benchmark::GenerateScene(spec);
  REQUIRE(scene);
  REQUIRE(vkgsplat::benchmark::WritePly(path, *scene));
  auto hash = vkgsplat::benchmark::FingerprintFile(path);
  REQUIRE(hash);
  vkgsplat::benchmark::FixtureInfo const info{
    .scene = spec,
    .hash = *hash,
    .bytes = std::filesystem::file_size(path),
    .canonical = false,
  };
  REQUIRE(vkgsplat::benchmark::WriteManifest(path, info));
  REQUIRE(vkgsplat::benchmark::VerifyFixture(path));
  {
    std::ofstream stream{ path, std::ios::binary | std::ios::app };
    stream.put('x');
  }
  REQUIRE_FALSE(vkgsplat::benchmark::VerifyFixture(path));
  REQUIRE(vkgsplat::benchmark::WritePly(path, *scene));
  REQUIRE(vkgsplat::benchmark::VerifyFixture(path));
  {
    std::ofstream stream{ vkgsplat::benchmark::ManifestPath(path), std::ios::binary | std::ios::app };
    stream.put('x');
  }
  REQUIRE_FALSE(vkgsplat::benchmark::VerifyFixture(path));
}

TEST_CASE("invalid fixture inputs are rejected", "[fixture]")
{
  REQUIRE_FALSE(vkgsplat::benchmark::GenerateScene({ .gaussian_count = 0U }));
  REQUIRE_FALSE(vkgsplat::benchmark::GenerateScene({ .gaussian_count = kSmallCount, .generator_version = 2U }));
  REQUIRE_FALSE(vkgsplat::benchmark::WritePly({}, {}));
  auto scene = vkgsplat::benchmark::GenerateScene({ .gaussian_count = kSmallCount });
  REQUIRE(scene);
  scene->appearances.pop_back();
  TempFixture const fixture{ "invalid" };
  REQUIRE_FALSE(vkgsplat::benchmark::WritePly(fixture.directory / "invalid.ply", *scene));
  scene->appearances.push_back({});
  scene->geometries.front().opacity = std::numeric_limits<vkgsplat::f32>::quiet_NaN();
  REQUIRE_FALSE(vkgsplat::benchmark::WritePly(fixture.directory / "nan.ply", *scene));
}

TEST_CASE("baseline profile identifies only the canonical scene", "[fixture]")
{
  auto const profile = vkgsplat::benchmark::BaselineProfile();
  REQUIRE(profile.id == vkgsplat::benchmark::kBaselineId);
  REQUIRE(profile.scene.gaussian_count == vkgsplat::benchmark::kBaselineCount);
  REQUIRE(vkgsplat::benchmark::IsCanonical(profile.scene));
  REQUIRE_FALSE(vkgsplat::benchmark::IsCanonical({ .gaussian_count = kSmallCount }));
  REQUIRE(profile.width == vkgsplat::benchmark::kBaselineWidth);
  REQUIRE(profile.height == vkgsplat::benchmark::kBaselineHeight);
  REQUIRE(profile.camera.aspect_ratio == static_cast<vkgsplat::f64>(profile.width) / profile.height);
  glm::dvec3 const expected_position{ 0.0, 0.0, vkgsplat::benchmark::kCameraDistance };
  REQUIRE(glm::distance(profile.camera.position, expected_position) == 0.0);
  REQUIRE(profile.sh_degree == vkgsplat::gs::kShDegree);
  REQUIRE_FALSE(profile.enable_imgui);
  REQUIRE_FALSE(profile.enable_gpu_timers);
  REQUIRE_FALSE(profile.enable_validation);
  REQUIRE(profile.frame_rate_mode == vkgsplat::FrameRateMode::kUncapped);
}

TEST_CASE("full baseline PLY keeps its frozen checksum", "[fixture]")
{
  TempFixture const fixture{ "baseline-hash" };
  auto scene = vkgsplat::benchmark::GenerateScene(vkgsplat::benchmark::BaselineProfile().scene);
  REQUIRE(scene);
  auto const path = fixture.directory / "baseline-v1.ply";
  REQUIRE(vkgsplat::benchmark::WritePly(path, *scene));
  auto hash = vkgsplat::benchmark::FingerprintFile(path);
  REQUIRE(hash);
  REQUIRE(*hash == vkgsplat::benchmark::kBaselinePlyHash);
  vkgsplat::benchmark::FixtureInfo const info{
    .scene = vkgsplat::benchmark::BaselineProfile().scene,
    .hash = *hash,
    .bytes = std::filesystem::file_size(path),
    .canonical = true,
  };
  REQUIRE(vkgsplat::benchmark::WriteManifest(path, info));
  REQUIRE(vkgsplat::benchmark::VerifyFixture(path));
  {
    std::fstream stream{ path, std::ios::binary | std::ios::in | std::ios::out };
    stream.seekp(-1, std::ios::end);
    stream.put('x');
  }
  auto forged = info;
  forged.hash = *vkgsplat::benchmark::FingerprintFile(path);
  REQUIRE(forged.hash != info.hash);
  REQUIRE_FALSE(vkgsplat::benchmark::WriteManifest(path, forged));
  REQUIRE_FALSE(vkgsplat::benchmark::VerifyFixture(path));
}

TEST_CASE("distant Gaussian samples remain stable", "[fixture]")
{
  auto scene = vkgsplat::benchmark::GenerateScene(vkgsplat::benchmark::BaselineProfile().scene);
  REQUIRE(scene);
  REQUIRE(scene->geometries.at(kMiddleSampleIndex).position.at(kXAxis) == kMiddleSampleX);
  REQUIRE(scene->geometries.at(kLastSampleIndex).position.at(kYAxis) == kLastSampleY);
  REQUIRE(scene->appearances.at(kLastSampleIndex).f_rest.at(0) == kLastSampleRest);
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
