#include "benchmark/fixture/ply_writer.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_headless/offscreen_platform.hpp>
#include <vkgsplat_io/splat_cpu.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr vkgsplat::u32 kTestWidth = 64U;
constexpr vkgsplat::u32 kTestHeight = 64U;
constexpr vkgsplat::u32 kRgbaChannels = 4U;
constexpr vkgsplat::u64 kFrameCount = 10U;
constexpr vkgsplat::u64 kInterleavedFrameCount = 3U;
constexpr vkgsplat::u32 kQuiltColumns = 2U;
constexpr vkgsplat::u32 kQuiltRows = 2U;
constexpr vkgsplat::u32 kColorTolerance = 4U;
constexpr vkgsplat::u32 kRedChannel = 0U;
constexpr vkgsplat::u32 kGreenChannel = 1U;
constexpr vkgsplat::u32 kBlueChannel = 2U;
constexpr vkgsplat::f64 kCameraDistance = 6.0;
constexpr vkgsplat::f64 kCameraSideOffset = 2.0;
constexpr vkgsplat::f64 kCompactViewConeDegrees = 8.0;
constexpr vkgsplat::f32 kCompactOffset = 0.35F;
constexpr vkgsplat::f32 kCompactLogScale = -0.3F;
constexpr vkgsplat::f32 kCompactOpacityLogit = 4.0F;
constexpr vkgsplat::f32 kCompactShDc = 1.0F / vkgsplat::gs::kShC0;

[[nodiscard]] auto WriteCompactQuiltScene(std::filesystem::path const &path) -> std::expected<void, vkgsplat::Error>
{
  // Four broad splats near the focal plane remain inside every narrow-cone quilt view.
  vkgsplat::SplatCpuData scene;
  for (auto const x : { -kCompactOffset, kCompactOffset }) {
    for (auto const y : { -kCompactOffset, kCompactOffset }) {
      scene.geometries.push_back(vkgsplat::gs::GaussianGeometry{
        .position = { x, y, 0.0F },
        .scale = { kCompactLogScale, kCompactLogScale, kCompactLogScale },
        .opacity = kCompactOpacityLogit,
      });
      scene.appearances.push_back(vkgsplat::gs::GaussianAppearance{
        .f_dc = { kCompactShDc, kCompactShDc, kCompactShDc },
      });
    }
  }
  return vkgsplat::benchmark::WritePly(path, scene);
}

[[nodiscard]] auto HasScenePixels(vkgsplat::RgbaFrame const &frame,
  vkgsplat::u32 origin_x,
  vkgsplat::u32 origin_y,
  vkgsplat::u32 width,
  vkgsplat::u32 height) -> bool
{
  for (vkgsplat::u32 y = origin_y; y < origin_y + height; ++y) {
    for (vkgsplat::u32 x = origin_x; x < origin_x + width; ++x) {
      auto const pixel_offset = (static_cast<size_t>(y) * frame.width + x) * kRgbaChannels;
      if (frame.pixels.at(pixel_offset + kRedChannel) > kColorTolerance
          || frame.pixels.at(pixel_offset + kGreenChannel) > kColorTolerance
          || frame.pixels.at(pixel_offset + kBlueChannel) > kColorTolerance) {
        return true;
      }
    }
  }
  return false;
}

auto Run(std::string const &scene_path, std::string const &compact_path, bool validation) -> int
{
  auto platform = vkgsplat::OffscreenPlatform::create({ .width = kTestWidth, .height = kTestHeight });
  if (!platform) {
    std::cerr << platform.error().message() << '\n';
    return EXIT_FAILURE;
  }
  vkgsplat::RendererConfig config{
    .ply_path = scene_path,
    .enable_validation = validation,
    .enable_imgui = false,
    .submission_policy = vkgsplat::FrameSubmissionPolicy::kLossless,
  };
  auto renderer = vkgsplat::Renderer::create(config, *platform);
  if (!renderer) {
    std::cerr << renderer.error().message() << '\n';
    return EXIT_FAILURE;
  }
  vkgsplat::Camera const camera_a{ vkgsplat::CameraConfig{
    .position = { 0.0, 0.0, kCameraDistance },
    .target = { 0.0, 0.0, 0.0 },
  } };
  vkgsplat::Camera const camera_b{ vkgsplat::CameraConfig{
    .position = { kCameraSideOffset, 0.0, kCameraDistance },
    .target = { 0.0, 0.0, 0.0 },
  } };
  auto first = renderer->render_frame(camera_a);
  if (!first) {
    std::cerr << first.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (first->width != kTestWidth || first->height != kTestHeight
      || first->pixels.size() != static_cast<size_t>(kTestWidth) * kTestHeight * kRgbaChannels) {
    std::cerr << "Unexpected offscreen frame extent\n";
    return EXIT_FAILURE;
  }
  if (!HasScenePixels(*first, 0U, 0U, kTestWidth, kTestHeight)) {
    std::cerr << "Offscreen scene is empty\n";
    return EXIT_FAILURE;
  }
  for (vkgsplat::u64 index = 1U; index < kFrameCount; ++index) {
    auto frame = renderer->render_frame(camera_a);
    if (!frame) {
      std::cerr << frame.error().message() << '\n';
      return EXIT_FAILURE;
    }
    if (frame->pixels != first->pixels) {
      std::cerr << "Repeated camera changed pixels\n";
      return EXIT_FAILURE;
    }
  }
  if (renderer->completed_frame_count() != kFrameCount) {
    std::cerr << "Capture count differs from request count\n";
    return EXIT_FAILURE;
  }
  auto frame_b = renderer->render_frame(camera_b);
  if (!frame_b) {
    std::cerr << frame_b.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (frame_b->pixels == first->pixels) {
    std::cerr << "Different camera did not change pixels\n";
    return EXIT_FAILURE;
  }
  if (auto drawn = renderer->draw(camera_a); !drawn) {
    std::cerr << drawn.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto captured_b = renderer->render_frame(camera_b);
  if (!captured_b) {
    std::cerr << captured_b.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (captured_b->pixels != frame_b->pixels) {
    std::cerr << "Interleaved capture returned the wrong camera frame\n";
    return EXIT_FAILURE;
  }
  if (renderer->completed_frame_count() != kFrameCount + kInterleavedFrameCount) {
    std::cerr << "Interleaved frame count differs from request count\n";
    return EXIT_FAILURE;
  }

  if (auto written = WriteCompactQuiltScene(compact_path); !written) {
    std::cerr << written.error().message() << '\n';
    return EXIT_FAILURE;
  }
  config.ply_path = compact_path;
  config.view_cone_deg = kCompactViewConeDegrees;
  config.lfd_grid = { kQuiltColumns, kQuiltRows };
  config.quilt_size = { kTestWidth, kTestHeight };
  auto quilt_renderer = vkgsplat::Renderer::create(config, *platform);
  if (!quilt_renderer) {
    std::cerr << quilt_renderer.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto quilt = quilt_renderer->render_frame(camera_a);
  if (!quilt) {
    std::cerr << quilt.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (quilt->width != kTestWidth || quilt->height != kTestHeight
      || quilt->pixels.size() != static_cast<size_t>(kTestWidth) * kTestHeight * kRgbaChannels) {
    std::cerr << "Unexpected quilt extent\n";
    return EXIT_FAILURE;
  }
  auto const tile_width = kTestWidth / kQuiltColumns;
  auto const tile_height = kTestHeight / kQuiltRows;
  for (vkgsplat::u32 row = 0U; row < kQuiltRows; ++row) {
    for (vkgsplat::u32 col = 0U; col < kQuiltColumns; ++col) {
      if (!HasScenePixels(*quilt, col * tile_width, row * tile_height, tile_width, tile_height)) {
        std::cerr << "Quilt tile has no scene pixels\n";
        return EXIT_FAILURE;
      }
    }
  }
  return EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  constexpr int kExpectedArgumentCount = 4;
  if (argc != kExpectedArgumentCount) { return EXIT_FAILURE; }
  std::string_view const validation_arg{ argv[3] };
  bool const validation = validation_arg == "ON" || validation_arg == "TRUE" || validation_arg == "1";
  return Run(argv[1], argv[2], validation);
}
