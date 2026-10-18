#ifndef VKGSPLAT_BENCHMARK_RENDER_THROUGHPUT_HPP
#define VKGSPLAT_BENCHMARK_RENDER_THROUGHPUT_HPP

#include "benchmark/fixture/profile.hpp"
#include <filesystem>
#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <atomic>
#include <expected>

namespace vkgsplat::benchmark {

inline constexpr u64 kWarmupFrames = 64U;
inline constexpr u64 kBatchFrames = 64U;

[[nodiscard]] auto RenderBatch(Renderer &renderer, Camera const &camera, u64 frame_count) -> std::expected<void, Error>;
auto RegisterRenderThroughput(Platform &platform,
  BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  Camera const &camera,
  std::atomic_bool &failed) -> void;

}// namespace vkgsplat::benchmark

#endif// VKGSPLAT_BENCHMARK_RENDER_THROUGHPUT_HPP
