#include "benchmark/batch_accounting.hpp"
#include "benchmark/fixture/profile.hpp"
#include "benchmark/fixture/renderer_adapter.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_headless/headless_platform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>

namespace {
constexpr int kSkipped = 77;
constexpr int kExpectedArguments = 2;
constexpr std::array<vkgsplat::u64, 4> kBatchSizes{ 1U, 2U, 3U, 64U };

auto Run(std::filesystem::path const &scene_path) -> int
{
  auto profile = vkgsplat::benchmark::BaselineProfile();
  profile.enable_validation = true;
  auto platform = vkgsplat::HeadlessPlatform::create({ .width = profile.width, .height = profile.height });
  if (!platform) {
    std::cerr << platform.error().message() << '\n';
    return kSkipped;
  }
  auto renderer =
    vkgsplat::Renderer::create(vkgsplat::benchmark::MakeGpuProfilingRendererConfig(profile, scene_path), *platform);
  if (!renderer) {
    std::cerr << renderer.error().message() << '\n';
    if (renderer.error().code() == std::make_error_code(std::errc::function_not_supported)) { return kSkipped; }
    return EXIT_FAILURE;
  }
  vkgsplat::Camera const camera{ profile.camera };
  std::optional<vkgsplat::u64> last_frame_id;
  for (auto const batch_size : kBatchSizes) {
    for (int repetition = 0; repetition < 2; ++repetition) {
      auto const completed_before = renderer->completed_frame_count();
      auto const recreated_before = renderer->recreated_frame_count();
      for (vkgsplat::u64 frame = 0U; frame < batch_size; ++frame) {
        if (auto drawn = renderer->draw(camera); !drawn) {
          std::cerr << drawn.error().message() << '\n';
          return EXIT_FAILURE;
        }
      }
      if (auto idle = renderer->wait_idle(); !idle) {
        std::cerr << idle.error().message() << '\n';
        return EXIT_FAILURE;
      }
      if (auto accounted = vkgsplat::benchmark::ValidateBatchAccounting(batch_size,
            completed_before,
            renderer->completed_frame_count(),
            recreated_before,
            renderer->recreated_frame_count());
        !accounted) {
        std::cerr << accounted.error().message() << '\n';
        return EXIT_FAILURE;
      }
      auto samples = renderer->take_gpu_timings();
      if (!samples || samples->size() != batch_size) { return EXIT_FAILURE; }
      std::unordered_set<vkgsplat::u64> frame_ids;
      for (auto const &sample : *samples) {
        if (!frame_ids.insert(sample.frame_id).second || (last_frame_id && sample.frame_id <= *last_frame_id)
            || !std::isfinite(sample.frame_ms) || sample.frame_ms < 0.0
            || sample.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kMultiviewProjection))) {
          return EXIT_FAILURE;
        }
        last_frame_id = sample.frame_id;
        for (size_t pass = 0U; pass < static_cast<size_t>(vkgsplat::GpuPass::kMultiviewProjection); ++pass) {
          auto const duration = sample.pass_ms.at(pass);
          if (!duration || !std::isfinite(*duration) || *duration < 0.0) { return EXIT_FAILURE; }
        }
      }
      auto empty = renderer->take_gpu_timings();
      if (!empty || !empty->empty()) { return EXIT_FAILURE; }
    }
  }
  return EXIT_SUCCESS;
}
}// namespace

auto main(int argc, char **argv) -> int
{
  if (argc != kExpectedArguments) { return EXIT_FAILURE; }
  return Run(argv[1]);
}
