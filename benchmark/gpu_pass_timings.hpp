#ifndef VKGSPLAT_BENCHMARK_GPU_PASS_TIMINGS_HPP
#define VKGSPLAT_BENCHMARK_GPU_PASS_TIMINGS_HPP

#include "benchmark/fixture/profile.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/types.hpp>

#include <atomic>
#include <filesystem>

namespace vkgsplat::benchmark {

auto RegisterGpuPassTimings(Platform &platform,
  BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  Camera const &camera,
  std::atomic_bool &failed) -> void;

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_GPU_PASS_TIMINGS_HPP
