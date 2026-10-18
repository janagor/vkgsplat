#ifndef VKGSPLAT_BENCHMARK_FIXTURE_SCENE_HPP
#define VKGSPLAT_BENCHMARK_FIXTURE_SCENE_HPP

#include "benchmark/fixture/profile.hpp"

#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/error.hpp>

#include <expected>

namespace vkgsplat::benchmark {

[[nodiscard]] auto GenerateScene(BenchmarkSceneSpec const &spec) -> std::expected<SplatCpuData, Error>;

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_FIXTURE_SCENE_HPP
