#include "app_config.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <expected>
#include <iterator>
#include <span>
#include <string>
#include <system_error>

#include <CLI/CLI.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>

namespace vkgsplat::app {

namespace {

  constexpr f64 kMaxFovDegreesExclusive = 180.0;

  [[nodiscard]] auto ParseFrameRateOption(std::string const &value) -> std::expected<vkgsplat::FrameRateConfig, Error>
  {
    if (value == "display") {
      return vkgsplat::FrameRateConfig{ .mode = vkgsplat::FrameRateMode::kDisplay, .fixed_fps = 0 };
    }
    if (value == "adaptive") {
      return vkgsplat::FrameRateConfig{ .mode = vkgsplat::FrameRateMode::kAdaptive, .fixed_fps = 0 };
    }

    u32 fps = 0;
    char const *const first = value.data();
    char const *const last = std::next(first, static_cast<std::ptrdiff_t>(value.size()));
    auto const [ptr, ec] = std::from_chars(first, last, fps);
    if (ec != std::errc{} || ptr != last || fps == 0U) {
      return std::unexpected{ MakeError(std::errc::invalid_argument,
        "invalid --frame-rate value '" + value + "' (expected positive integer, 'display', or 'adaptive')") };
    }

    return vkgsplat::FrameRateConfig{ .mode = vkgsplat::FrameRateMode::kFixed, .fixed_fps = fps };
  }

  [[nodiscard]] auto ToArray(glm::dvec3 const &vec) -> std::array<f64, 3>
  {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return { vec[0], vec[1], vec[2] };
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  }

  [[nodiscard]] auto FromArray(std::array<f64, 3> const &components) -> glm::dvec3
  {
    return { components.at(0), components.at(1), components.at(2) };
  }

}// namespace

auto ParseAppConfig(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>
{
  AppConfig config{};
  std::string frame_rate_text{};

  auto position = ToArray(config.camera.position);
  auto target = ToArray(config.camera.target);
  auto world_up = ToArray(config.camera.up);

  CLI::App app{ "vkgsplat" };

  // Optional INI/TOML config. CLI arguments override file values.
  app.set_config("--config", "", "Read an INI/TOML config file (CLI overrides file)")
    ->check(CLI::ExistingFile);

  app.add_flag("--validation", config.enable_validation, "Enable Vulkan validation layers");
  app.add_flag("--imgui,!--no-imgui",
    config.enable_imgui,
    "Enable ImGui overlay (default on; use --no-imgui or imgui=false in config to disable)");
  app.add_flag("--gpu-timers", config.enable_gpu_timers, "Enable in-app Vulkan GPU pass timestamps");
  app.add_option("--frame-rate",
    frame_rate_text,
    "Cap/stabilize FPS via VK_EXT_present_timing: <N>, 'display', or 'adaptive'");
  app.add_option("--camera-position", position, "Camera starting position as x y z")->capture_default_str();
  app.add_option("--camera-target", target, "Camera look-at target as x y z")->capture_default_str();
  app.add_option("--camera-up", world_up, "Camera world-up vector as x y z")->capture_default_str();
  app.add_option("--camera-fov", config.camera.fov_degrees, "Vertical field of view in degrees")
    ->capture_default_str();
  app.add_option("--camera-near", config.camera.near_plane, "Camera near plane")->capture_default_str();
  app.add_option("--camera-far", config.camera.far_plane, "Camera far plane")->capture_default_str();
  app.add_option("--camera-aspect",
    config.camera.aspect_ratio,
    "Preferred aspect ratio (windowed mode still uses the swapchain aspect)")
    ->capture_default_str();
  app.add_option("--camera-speed", config.camera.movement_speed, "Camera movement speed")->capture_default_str();
  app.add_option("--camera-sensitivity", config.camera.mouse_sensitivity, "Mouse look sensitivity")
    ->capture_default_str();
  // Positional, --ply-path, or config key `ply_path` / `ply-path`.
  app.add_option("ply_path,-p,--ply-path", config.ply_path, "Path to PLY file")->check(CLI::ExistingFile);

  try {
    app.parse(static_cast<int>(args.size()), args.data());
  } catch (CLI::ParseError const &parse_error) {
    auto const exit_code = app.exit(parse_error);
    if (exit_code == static_cast<int>(CLI::ExitCodes::Success)) {
      return std::unexpected{ MakeError(std::errc::operation_canceled, "help requested") };
    }
    return std::unexpected{ MakeError(std::errc::invalid_argument, parse_error.what()) };
  }

  config.camera.position = FromArray(position);
  config.camera.target = FromArray(target);
  config.camera.up = FromArray(world_up);

  if (config.ply_path.empty()) {
    return std::unexpected{ MakeError(std::errc::invalid_argument,
      "missing PLY path (pass ply_path, --ply-path, or set ply_path / ply-path in --config)") };
  }

  if (config.camera.position == config.camera.target) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "camera-position and camera-target must differ") };
  }
  if (glm::length(config.camera.up) == 0.0) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "camera-up must be a non-zero vector") };
  }
  if (!(config.camera.near_plane > 0.0) || !(config.camera.far_plane > config.camera.near_plane)) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "require 0 < camera-near < camera-far") };
  }
  if (!(config.camera.fov_degrees > 0.0) || !(config.camera.fov_degrees < kMaxFovDegreesExclusive)) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "camera-fov must be in (0, 180) degrees") };
  }
  if (!(config.camera.aspect_ratio > 0.0) || !std::isfinite(config.camera.aspect_ratio)) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "camera-aspect must be a positive finite value") };
  }

  if (!frame_rate_text.empty()) {
    auto parsed = ParseFrameRateOption(frame_rate_text);
    if (!parsed) { return std::unexpected{ parsed.error() }; }
    config.frame_rate = *parsed;
  }

  return config;
}

}// namespace vkgsplat::app
