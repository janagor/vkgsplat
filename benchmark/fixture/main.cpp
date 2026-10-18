#include "benchmark/fixture/manifest.hpp"
#include "benchmark/fixture/ply_writer.hpp"
#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/scene.hpp"

#include <CLI/CLI.hpp>
#include <vkgsplat_utility/types.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <system_error>

namespace {

inline constexpr int kChecksumHexWidth = 16;

auto Run(int argc, char **argv) -> int
{
  CLI::App app{ "Generate and verify deterministic 3DGS benchmark fixtures" };
  std::string profile;
  std::filesystem::path output;
  std::filesystem::path verify;
  vkgsplat::u32 count = vkgsplat::benchmark::kBaselineCount;
  vkgsplat::u64 seed = vkgsplat::benchmark::kBaselineSeed;
  auto *profile_option = app.add_option("--profile", profile, "Fixture profile ID");
  auto *output_option = app.add_option("--output", output, "Output PLY path");
  auto *count_option = app.add_option("--count", count, "Gaussian count override");
  auto *seed_option = app.add_option("--seed", seed, "Seed override (decimal integer)");
  app.add_option("--verify", verify, "Verify a PLY and its adjacent manifest");
  try {
    app.parse(argc, argv);
  } catch (CLI::ParseError const &error) {
    return app.exit(error);
  }
  if (!verify.empty()) {
    if (profile_option->count() != 0 || output_option->count() != 0 || count_option->count() != 0
        || seed_option->count() != 0) {
      std::cerr << "--verify cannot be combined with generation options\n";
      return EXIT_FAILURE;
    }
    auto const result = vkgsplat::benchmark::VerifyFixture(verify);
    if (!result) {
      std::cerr << "Verification failed: " << result.error().message() << '\n';
      return EXIT_FAILURE;
    }
    std::cout << "Verified " << verify
              << ": profile=" << (result->canonical ? vkgsplat::benchmark::kBaselineId : vkgsplat::benchmark::kCustomId)
              << " gaussians=" << result->scene.gaussian_count << " generator=" << result->scene.generator_version
              << " bytes=" << result->bytes << " checksum=" << std::hex << std::setw(kChecksumHexWidth)
              << std::setfill('0') << result->hash << '\n';
    return EXIT_SUCCESS;
  }
  if (profile != vkgsplat::benchmark::kBaselineId || output.empty()) {
    std::cerr << "Generation requires --profile baseline-v1 and --output PATH\n";
    return EXIT_FAILURE;
  }
  vkgsplat::benchmark::BenchmarkSceneSpec const spec{ .gaussian_count = count, .seed = seed };
  auto scene = vkgsplat::benchmark::GenerateScene(spec);
  if (!scene) {
    std::cerr << "Generation failed: " << scene.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (auto result = vkgsplat::benchmark::WritePly(output, *scene); !result) {
    std::cerr << "PLY write failed: " << result.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto hash = vkgsplat::benchmark::FingerprintFile(output);
  if (!hash) {
    std::cerr << "Checksum failed: " << hash.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (vkgsplat::benchmark::IsCanonical(spec) && *hash != vkgsplat::benchmark::kBaselinePlyHash) {
    std::cerr << "Generated PLY does not match the pinned baseline-v1 checksum\n";
    return EXIT_FAILURE;
  }
  std::error_code error;
  auto const bytes = std::filesystem::file_size(output, error);
  if (error) {
    std::cerr << "File size failed: " << error.message() << '\n';
    return EXIT_FAILURE;
  }
  vkgsplat::benchmark::FixtureInfo const info{
    .scene = spec,
    .hash = *hash,
    .bytes = bytes,
    .canonical = vkgsplat::benchmark::IsCanonical(spec),
  };
  if (auto result = vkgsplat::benchmark::WriteManifest(output, info); !result) {
    std::cerr << "Manifest write failed: " << result.error().message() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "Generated " << output
            << ": profile=" << (info.canonical ? vkgsplat::benchmark::kBaselineId : vkgsplat::benchmark::kCustomId)
            << " gaussians=" << spec.gaussian_count << " generator=" << spec.generator_version << " bytes=" << bytes
            << " checksum=" << std::hex << std::setw(kChecksumHexWidth) << std::setfill('0') << *hash << '\n';
  return EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  try {
    return Run(argc, argv);
  } catch (std::exception const &error) {
    std::cerr << "Fixture generator failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
