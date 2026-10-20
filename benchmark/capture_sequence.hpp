#ifndef VKGSPLAT_BENCHMARK_CAPTURE_SEQUENCE_HPP
#define VKGSPLAT_BENCHMARK_CAPTURE_SEQUENCE_HPP

#include <atomic>
#include <filesystem>

namespace vkgsplat {
class Camera;
namespace benchmark {
struct BenchmarkProfile;
auto RegisterCaptureSequence(BenchmarkProfile const &profile,
  std::filesystem::path const &scene_path,
  Camera const &camera,
  std::atomic_bool &failed) -> void;
}// namespace benchmark
}// namespace vkgsplat

#endif// VKGSPLAT_BENCHMARK_CAPTURE_SEQUENCE_HPP
