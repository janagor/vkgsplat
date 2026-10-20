#include <vkgsplat/camera.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_headless/headless_platform.hpp>
#include <vkgsplat_headless/offscreen_platform.hpp>
#include <vkgsplat_capture/frame_sequence.hpp>
#include <vkgsplat_capture/screenshot.hpp>
#include <vkgsplat_utility/types.hpp>

#include <CLI/CLI.hpp>
#include <glm/ext/vector_double3.hpp>

#include <array>
#include <cmath>
#include <exception>
#include <numbers>
#include <memory>
#include <print>
#include <string>
#include <utility>

namespace {
constexpr vkgsplat::u32 kDefaultSequenceFps = 60U;
constexpr vkgsplat::f64 kDefaultOrbitDegrees = 360.0;
constexpr vkgsplat::f64 kFullCircleDegrees = 360.0;
constexpr vkgsplat::f64 kRadiansPerCircle = 2.0 * std::numbers::pi_v<vkgsplat::f64>;

[[nodiscard]] auto ToArray(glm::dvec3 const &vector) -> std::array<vkgsplat::f64, 3>
{
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return { vector[0], vector[1], vector[2] };
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

auto RunHeadless(int argc, char **argv) -> int
{
  CLI::App app{ "Render Gaussian Splatting frames without a window" };
  std::string ply_path;
  std::string output;
  std::string sequence_dir;
  vkgsplat::u64 frames = 0U;
  vkgsplat::u32 fps = kDefaultSequenceFps;
  vkgsplat::u32 fps_denominator = 1U;
  vkgsplat::f64 orbit_degrees = kDefaultOrbitDegrees;
  std::string target = "headless-surface";
  vkgsplat::u32 width = vkgsplat::kDefaultHeadlessWidth;
  vkgsplat::u32 height = vkgsplat::kDefaultHeadlessHeight;
  bool validation = false;
  auto const default_camera = vkgsplat::CameraConfig{};
  auto camera_position = ToArray(default_camera.position);
  auto camera_target = ToArray(default_camera.target);
  app.add_option("--ply", ply_path, "PLY scene path")->required()->check(CLI::ExistingFile);
  auto *output_option = app.add_option("--output", output, "Output PNG path");
  auto *sequence_option = app.add_option("--sequence-dir", sequence_dir, "Output directory for a PNG sequence");
  output_option->excludes(sequence_option);
  app.add_option("--frames", frames, "Number of sequence frames");
  app.add_option("--fps", fps, "Frame rate numerator");
  app.add_option("--fps-denominator", fps_denominator, "Frame rate denominator");
  app.add_option("--orbit-degrees", orbit_degrees, "Total horizontal orbit angle");
  app.add_option("--target", target, "Output target: headless-surface or offscreen")
    ->check(CLI::IsMember({ "headless-surface", "offscreen" }));
  app.add_option("--width", width, "Frame width")->capture_default_str();
  app.add_option("--height", height, "Frame height")->capture_default_str();
  app.add_flag("--validation", validation, "Enable Vulkan validation layers");
  app.add_option("--camera-position", camera_position, "Camera position as x y z");
  app.add_option("--camera-target", camera_target, "Camera look-at target as x y z");
  try {
    app.parse(argc, argv);
  } catch (CLI::ParseError const &error) {
    return app.exit(error);
  }

  if ((output.empty() && sequence_dir.empty()) || width == 0U || height == 0U || fps == 0U
      || fps_denominator == 0U || !std::isfinite(orbit_degrees)
      || (!sequence_dir.empty() && (target != "offscreen" || frames == 0U))) {
    std::println(stderr, "Invalid output mode, positive width and height, frame count, frame rate, or orbit angle");
    return 1;
  }

  std::unique_ptr<vkgsplat::Platform> platform;
  if (target == "offscreen") {
    auto created = vkgsplat::OffscreenPlatform::create({ .width = width, .height = height });
    if (!created) {
      std::println(stderr, "{}", created.error().message());
      return 1;
    }
    platform = std::make_unique<vkgsplat::OffscreenPlatform>(std::move(*created));
  } else {
    auto created = vkgsplat::HeadlessPlatform::create({ .width = width, .height = height });
    if (!created) {
      std::println(stderr, "{}", created.error().message());
      return 1;
    }
    platform = std::make_unique<vkgsplat::HeadlessPlatform>(std::move(*created));
  }
  vkgsplat::RendererConfig const config{
    .ply_path = ply_path,
    .enable_validation = validation,
    .enable_imgui = false,
    .enable_gpu_timers = false,
    .submission_policy = vkgsplat::FrameSubmissionPolicy::kLossless,
    .frame_rate = { .mode = vkgsplat::FrameRateMode::kUncapped },
    .lfd_view_order = {},
  };
  auto renderer = vkgsplat::Renderer::create(config, *platform);
  if (!renderer) {
    std::println(stderr, "{}", renderer.error().message());
    return 1;
  }
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{
    .position = { camera_position.at(0), camera_position.at(1), camera_position.at(2) },
    .target = { camera_target.at(0), camera_target.at(1), camera_target.at(2) },
    .aspect_ratio = static_cast<vkgsplat::f64>(width) / static_cast<vkgsplat::f64>(height),
  } };
  if (!sequence_dir.empty()) {
    auto sequence = vkgsplat::capture::FrameSequence::create(*renderer, {
      .output_directory = sequence_dir,
      .frame_rate = { .numerator = fps, .denominator = fps_denominator },
    });
    if (!sequence) {
      std::println(stderr, "{}", sequence.error().message());
      return 1;
    }
    auto const radians_per_degree = kRadiansPerCircle / kFullCircleDegrees;
    auto const dx = camera_position.at(0) - camera_target.at(0);
    auto const dz = camera_position.at(2) - camera_target.at(2);
    for (vkgsplat::u64 index = 0U; index < frames; ++index) {
      auto const angle = static_cast<vkgsplat::f64>(index) / static_cast<vkgsplat::f64>(frames)
                         * orbit_degrees * radians_per_degree;
      vkgsplat::Camera const orbit_camera{ vkgsplat::CameraConfig{
        .position = { camera_target.at(0) + dx * std::cos(angle) + dz * std::sin(angle),
          camera_position.at(1),
          camera_target.at(2) - dx * std::sin(angle) + dz * std::cos(angle) },
        .target = { camera_target.at(0), camera_target.at(1), camera_target.at(2) },
        .aspect_ratio = static_cast<vkgsplat::f64>(width) / static_cast<vkgsplat::f64>(height),
      } };
      auto recorded = sequence->record_frame(orbit_camera);
      if (!recorded) {
        std::println(stderr, "{}", recorded.error().message());
        return 1;
      }
    }
    auto result = sequence->finish();
    if (!result) {
      std::println(stderr, "{}", result.error().message());
      return 1;
    }
    std::println("Saved {} frames to {}", result->frame_count, sequence_dir);
    return 0;
  }
  if (target == "offscreen") {
    auto frame = renderer->render_frame(camera);
    if (!frame) {
      std::println(stderr, "{}", frame.error().message());
      return 1;
    }
    if (auto written = vkgsplat::capture::write_png(*frame, output); !written) {
      std::println(stderr, "{}", written.error().message());
      return 1;
    }
    std::println("Saved {}", output);
    return 0;
  }
  if (auto result = renderer->draw(camera); !result) {
    std::println(stderr, "{}", result.error().message());
    return 1;
  }
  if (auto result = renderer->wait_idle(); !result) {
    std::println(stderr, "{}", result.error().message());
    return 1;
  }
  if (renderer->completed_frame_count() != 1) {
    std::println(stderr, "Expected exactly one rendered frame; got {}", renderer->completed_frame_count());
    return 1;
  }
  if (auto result = vkgsplat::capture::save_png(*renderer, output); !result) {
    std::println(stderr, "{}", result.error().message());
    return 1;
  }
  std::println("Saved {}", output);
  return 0;
}
}// namespace

auto main(int argc, char **argv) -> int
{
  try {
    return RunHeadless(argc, argv);
  } catch (std::exception const &error) {
    try {
      std::println(stderr, "Headless rendering failed: {}", error.what());
    } catch (...) {
      return 1;
    }
    return 1;
  } catch (...) {
    return 1;
  }
}
