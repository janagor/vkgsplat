#include "benchmark/fixture/ply_writer.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_capture/frame_sequence.hpp>
#include <vkgsplat_headless/offscreen_platform.hpp>
#include <vkgsplat_io/splat_cpu.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

[[nodiscard]] auto ReadPng(std::filesystem::path const &path) -> std::optional<vkgsplat::RgbaFrame>
{
  int width{};
  int height{};
  int channels{};
  auto const filename = path.string();
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
    stbi_load(filename.c_str(), &width, &height, &channels, static_cast<int>(kRgbaChannels)), stbi_image_free
  };
  if (!pixels || width <= 0 || height <= 0) { return std::nullopt; }
  auto const byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * kRgbaChannels;
  return vkgsplat::RgbaFrame{ .width = static_cast<vkgsplat::u32>(width),
    .height = static_cast<vkgsplat::u32>(height), .pixels = { pixels.get(), pixels.get() + byte_count } };
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
  auto const sequence_path = std::filesystem::temp_directory_path()
    / ("vkgsplat-offscreen-sequence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  auto sequence = vkgsplat::capture::FrameSequence::create(*renderer, { .output_directory = sequence_path });
  if (!sequence) {
    std::cerr << sequence.error().message() << '\n';
    return EXIT_FAILURE;
  }
  std::vector<std::filesystem::path> sequence_files;
  for (auto const &capture_camera : { camera_a, camera_b, camera_a }) {
    auto recorded = sequence->record_frame(capture_camera);
    if (!recorded || recorded->width != kTestWidth || recorded->height != kTestHeight) {
      std::cerr << "Sequence frame capture failed\n";
      return EXIT_FAILURE;
    }
    sequence_files.push_back(recorded->path);
  }
  auto const summary = sequence->finish();
  if (!summary || summary->frame_count != kInterleavedFrameCount) {
    std::cerr << "Sequence did not finish\n";
    return EXIT_FAILURE;
  }
  std::vector<vkgsplat::RgbaFrame> decoded_frames;
  for (auto const &filename : sequence_files) {
    auto decoded = ReadPng(filename);
    if (!decoded || decoded->width != kTestWidth || decoded->height != kTestHeight
        || decoded->pixels.size() != first->pixels.size()) {
      std::cerr << "Sequence PNG is missing or has unexpected dimensions\n";
      return EXIT_FAILURE;
    }
    decoded_frames.push_back(std::move(*decoded));
  }
  if (decoded_frames.at(0).pixels != first->pixels || decoded_frames.at(1).pixels != frame_b->pixels
      || decoded_frames.at(2).pixels != decoded_frames.at(0).pixels
      || decoded_frames.at(1).pixels == decoded_frames.at(0).pixels) {
    std::cerr << "Sequence PNG pixels do not match their camera states\n";
    return EXIT_FAILURE;
  }
  std::error_code cleanup_error;
  std::filesystem::remove_all(sequence_path, cleanup_error);
  if (cleanup_error) {
    std::cerr << "Could not remove sequence test files\n";
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
  auto const quilt_sequence_path = std::filesystem::temp_directory_path()
    / ("vkgsplat-quilt-sequence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  auto quilt_sequence = vkgsplat::capture::FrameSequence::create(*quilt_renderer,
    { .output_directory = quilt_sequence_path });
  if (!quilt_sequence) {
    std::cerr << quilt_sequence.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto quilt_recorded = quilt_sequence->record_frame(camera_a);
  auto quilt_finished = quilt_sequence->finish();
  if (!quilt_recorded || quilt_recorded->width != kTestWidth || quilt_recorded->height != kTestHeight
      || !quilt_finished || quilt_finished->frame_count != 1U
      || !std::filesystem::is_regular_file(quilt_recorded->path)) {
    std::cerr << "Quilt sequence capture failed\n";
    return EXIT_FAILURE;
  }
  auto decoded_quilt = ReadPng(quilt_recorded->path);
  if (!decoded_quilt || decoded_quilt->width != kTestWidth || decoded_quilt->height != kTestHeight
      || decoded_quilt->pixels != quilt->pixels) {
    std::cerr << "Quilt sequence PNG pixels differ from the captured atlas\n";
    return EXIT_FAILURE;
  }
  std::filesystem::remove_all(quilt_sequence_path, cleanup_error);
  if (cleanup_error) {
    std::cerr << "Could not remove quilt sequence test files\n";
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
