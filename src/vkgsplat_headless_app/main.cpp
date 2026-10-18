#include <vkgsplat/camera.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_headless/headless_platform.hpp>
#include <vkgsplat_utility/types.hpp>

#include <CLI/CLI.hpp>
#include <glm/ext/vector_double3.hpp>

#include <array>
#include <exception>
#include <print>
#include <string>

namespace {
[[nodiscard]] auto ToArray(glm::dvec3 const &vector) -> std::array<vkgsplat::f64, 3>
{
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return { vector[0], vector[1], vector[2] };
  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

auto RunHeadless(int argc, char **argv) -> int
{
  CLI::App app{ "Render one Gaussian Splatting frame without a window" };
  std::string ply_path;
  std::string output;
  vkgsplat::u32 width = vkgsplat::kDefaultHeadlessWidth;
  vkgsplat::u32 height = vkgsplat::kDefaultHeadlessHeight;
  bool validation = false;
  auto const default_camera = vkgsplat::CameraConfig{};
  auto camera_position = ToArray(default_camera.position);
  auto camera_target = ToArray(default_camera.target);
  app.add_option("--ply", ply_path, "PLY scene path")->required()->check(CLI::ExistingFile);
  app.add_option("--output", output, "Output PNG path")->required();
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

  auto platform = vkgsplat::HeadlessPlatform::create({ .width = width, .height = height });
  if (!platform) {
    std::println(stderr, "{}", platform.error().message());
    return 1;
  }
  vkgsplat::RendererConfig const config{
    .ply_path = ply_path,
    .enable_validation = validation,
    .enable_imgui = false,
    .enable_gpu_timers = false,
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
  if (auto result = renderer->save_frame_png(output); !result) {
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
