#include "app_config.hpp"

#include <vkgsplat_utility/error.hpp>

#include <expected>
#include <span>
#include <string>
#include <system_error>

#include <CLI/CLI.hpp>

namespace vkgsplat::app {

auto parse_app_config(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>
{
  AppConfig config{};

  bool disable_imgui = false;

  CLI::App app{ "vkgsplat" };

  app.add_flag("--validation", config.enable_validation, "Enable Vulkan validation layers");
  app.add_flag("--no-imgui", disable_imgui, "Disable ImGui overlay (useful when profiling)");
  app.add_flag("--gpu-timers", config.enable_gpu_timers, "Enable in-app Vulkan GPU pass timestamps");
  app.add_option("-c,--count", config.splat_count, "Number of splats to load")
    ->check(CLI::PositiveNumber)
    ->capture_default_str();
  app.add_option("ply_path", config.ply_path, "Path to PLY file")->required()->check(CLI::ExistingFile);

  try {
    app.parse(static_cast<int>(args.size()), args.data());
  } catch (CLI::ParseError const &parse_error) {
    auto const exit_code = app.exit(parse_error);
    if (exit_code == static_cast<int>(CLI::ExitCodes::Success)) {
      return std::unexpected{ make_error(std::errc::operation_canceled, "help requested") };
    }
    return std::unexpected{ make_error(std::errc::invalid_argument, parse_error.what()) };
  }

  config.enable_imgui = !disable_imgui;
  return config;
}

}// namespace vkgsplat::app
