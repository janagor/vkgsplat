#ifndef VKGSPLAT_BENCHMARK_FIXTURE_PLY_WRITER_HPP
#define VKGSPLAT_BENCHMARK_FIXTURE_PLY_WRITER_HPP

#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <expected>
#include <filesystem>

namespace vkgsplat::benchmark {

inline constexpr u32 kPlyPropertiesPerVertex = 62U;
inline constexpr u32 kPlyBytesPerVertex = kPlyPropertiesPerVertex * sizeof(f32);

[[nodiscard]] auto WritePly(std::filesystem::path const &path, SplatCpuData const &scene) -> std::expected<void, Error>;
[[nodiscard]] auto FingerprintFile(std::filesystem::path const &path) -> std::expected<u64, Error>;

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_FIXTURE_PLY_WRITER_HPP
