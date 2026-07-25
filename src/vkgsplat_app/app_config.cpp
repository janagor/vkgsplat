#include "app_config.hpp"

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <exception>
#include <expected>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <CLI/CLI.hpp>

#ifndef VKGSPLAT_SOURCE_DIR
#define VKGSPLAT_SOURCE_DIR "."
#endif

namespace vkgsplat::app {

namespace {

  using vkgsplat::make_error;

  [[nodiscard]] auto default_ply_path() -> std::string
  { return std::string{ VKGSPLAT_SOURCE_DIR } + "/resources/scene.ply"; }

  [[nodiscard]] auto parse_positive_count(std::string const &text) -> std::expected<vkgsplat::u32, vkgsplat::Error>
  {
    try {
      if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        return std::unexpected{ make_error(std::errc::invalid_argument, "count must be a positive integer") };
      }
      unsigned long const parsed = std::stoul(text);
      if (parsed == 0UL) {
        return std::unexpected{ make_error(std::errc::invalid_argument, "count must be greater than zero") };
      }
      return static_cast<vkgsplat::u32>(parsed);
    } catch (std::exception const &) {
      return std::unexpected{ make_error(std::errc::result_out_of_range, "count is out of range for u32") };
    }
  }

}// namespace

auto parse_app_config(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>
{
  AppConfig config{};
  config.ply_path = default_ply_path();

  bool disable_imgui = false;
  std::vector<std::string> ply_option_args;
  vkgsplat::u32 positional_count = config.splat_count;
  std::string positional_ply_path;

  CLI::App app{ "vkgsplat" };

  app.add_flag("--validation", config.enable_validation, "Enable Vulkan validation layers");
  app.add_flag("--no-imgui", disable_imgui, "Disable ImGui overlay (useful when profiling)");
  app.add_flag("--gpu-timers", config.enable_gpu_timers, "Enable in-app Vulkan GPU pass timestamps");
  app.add_option("--ply", ply_option_args, "Splat count and optional PLY path")->expected(1, 2);
  app.add_option("count", positional_count, "Number of splats to load")
    ->check(CLI::PositiveNumber)
    ->expected(0, 1);
  app.add_option("ply_path", positional_ply_path, "Path to PLY file")->expected(0, 1);

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

  if (!ply_option_args.empty()) {
    auto const count = parse_positive_count(ply_option_args.front());
    if (!count) { return std::unexpected{ count.error() }; }
    config.splat_count = *count;
    if (ply_option_args.size() > 1U) { config.ply_path = ply_option_args.at(1); }
  } else if (app.count("count") > 0) {
    config.splat_count = positional_count;
  }

  if (!positional_ply_path.empty()) { config.ply_path = positional_ply_path; }

  return config;
}

}// namespace vkgsplat::app
