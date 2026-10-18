#ifndef VKGSPLAT_BENCHMARK_FIXTURE_RENDERER_ADAPTER_HPP
#define VKGSPLAT_BENCHMARK_FIXTURE_RENDERER_ADAPTER_HPP

#include "benchmark/fixture/profile.hpp"

#include <vkgsplat/renderer.hpp>

#include <filesystem>

namespace vkgsplat::benchmark {

[[nodiscard]] auto MakeRendererConfig(BenchmarkProfile const &profile, std::filesystem::path const &scene_path)
  -> RendererConfig;
[[nodiscard]] auto MakeGpuProfilingRendererConfig(BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path) -> RendererConfig;

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_FIXTURE_RENDERER_ADAPTER_HPP
