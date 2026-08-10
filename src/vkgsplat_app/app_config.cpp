#include "app_config.hpp"

#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <charconv>
#include <cstddef>
#include <expected>
#include <iterator>
#include <span>
#include <string>
#include <system_error>

#include <CLI/CLI.hpp>

namespace vkgsplat::app {

namespace {

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

}// namespace

auto ParseAppConfig(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>
{
  AppConfig config{};
  std::string frame_rate_text{};

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

  if (config.ply_path.empty()) {
    return std::unexpected{ MakeError(std::errc::invalid_argument,
      "missing PLY path (pass ply_path, --ply-path, or set ply_path / ply-path in --config)") };
  }

  if (!frame_rate_text.empty()) {
    auto parsed = ParseFrameRateOption(frame_rate_text);
    if (!parsed) { return std::unexpected{ parsed.error() }; }
    config.frame_rate = *parsed;
  }

  return config;
}

}// namespace vkgsplat::app
