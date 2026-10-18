#ifndef VKGSPLAT_BENCHMARK_FIXTURE_MANIFEST_HPP
#define VKGSPLAT_BENCHMARK_FIXTURE_MANIFEST_HPP

#include "benchmark/fixture/profile.hpp"

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <expected>
#include <filesystem>

namespace vkgsplat::benchmark {

struct FixtureInfo
{
  BenchmarkSceneSpec scene{};
  u64 hash{};
  u64 bytes{};
  bool canonical{};
};

[[nodiscard]] auto ManifestPath(std::filesystem::path const &ply_path) -> std::filesystem::path;
[[nodiscard]] auto WriteManifest(std::filesystem::path const &ply_path, FixtureInfo const &info)
  -> std::expected<void, Error>;
[[nodiscard]] auto VerifyFixture(std::filesystem::path const &ply_path) -> std::expected<FixtureInfo, Error>;

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_FIXTURE_MANIFEST_HPP
