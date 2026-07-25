#include "app_config.hpp"

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <expected>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#ifndef VKGSPLAT_SOURCE_DIR
#define VKGSPLAT_SOURCE_DIR "."
#endif

namespace vkgsplat::app {

namespace {

  using vkgsplat::make_error;

  [[nodiscard]] auto default_ply_path() -> std::string
  { return std::string{ VKGSPLAT_SOURCE_DIR } + "/resources/scene.ply"; }

  [[nodiscard]] auto is_unsigned_integer(std::string_view text) -> bool
  {
    if (text.empty()) { return false; }

    return std::ranges::all_of(
      text, [](char const character) { return std::isdigit(static_cast<unsigned char>(character)) != 0; });
  }

  [[nodiscard]] auto parse_count(std::string_view text) -> std::expected<vkgsplat::u32, vkgsplat::Error>
  {
    if (!is_unsigned_integer(text)) {
      return std::unexpected{ make_error(std::errc::invalid_argument, "count must be a positive integer") };
    }

    try {
      unsigned long const parsed = std::stoul(std::string{ text });
      if (parsed == 0UL) {
        return std::unexpected{ make_error(std::errc::invalid_argument, "count must be greater than zero") };
      }
      return static_cast<vkgsplat::u32>(parsed);
    } catch (std::exception const &) {
      return std::unexpected{ make_error(std::errc::result_out_of_range, "count is out of range for u32") };
    }
  }

  void print_usage(std::string_view program_name)
  {
    std::println(stderr, "Usage:");
    std::println(stderr, "  {} [--validation] [--no-imgui] [--gpu-timers] [--ply] <count> [ply_path]", program_name);
    std::println(stderr, "");
    std::println(stderr, "  --validation  enable Vulkan validation layers (off by default)");
    std::println(stderr, "  --no-imgui    disable ImGui overlay (useful when profiling)");
    std::println(stderr, "  --gpu-timers  enable in-app Vulkan GPU pass timestamps");
    std::println(stderr, "  --ply         load first N splats from a PLY file (default)");
    std::println(stderr, "  <count>       number of splats to load (default: 64)");
    std::println(stderr, "  [ply_path]    path to PLY file (default: resources/scene.ply)");
  }

  [[nodiscard]] auto bounded_arg(std::span<char *const> args, size_t index) -> char *
  { return args.subspan(index, 1).front(); }

  [[nodiscard]] auto require_count(std::span<char *const> args, size_t &index)
    -> std::expected<vkgsplat::u32, vkgsplat::Error>
  {
    if (index >= args.size()) {
      print_usage(args.front());
      return std::unexpected{ make_error(std::errc::invalid_argument, "missing <count> argument") };
    }

    auto const count = parse_count(bounded_arg(args, index));
    if (!count) {
      print_usage(args.front());
      return std::unexpected{ count.error() };
    }

    ++index;
    return count;
  }

  [[nodiscard]] auto parse_ply(std::span<char *const> args, size_t &index, AppConfig &config)
    -> std::expected<void, vkgsplat::Error>
  {
    ++index;

    auto const count = require_count(args, index);
    if (!count) { return std::unexpected{ count.error() }; }

    config.splat_count = *count;

    if (index < args.size() && !std::string_view{ bounded_arg(args, index) }.starts_with("--")) {
      config.ply_path = bounded_arg(args, index);
      ++index;
    }

    return {};
  }

  [[nodiscard]] auto parse_shorthand_ply(std::span<char *const> args, size_t &index, AppConfig &config)
    -> std::expected<void, vkgsplat::Error>
  {
    auto const count = parse_count(bounded_arg(args, index));
    if (!count) {
      print_usage(args.front());
      return std::unexpected{ count.error() };
    }

    config.splat_count = *count;
    ++index;

    if (index < args.size() && !std::string_view{ bounded_arg(args, index) }.starts_with("--")) {
      config.ply_path = bounded_arg(args, index);
      ++index;
    }

    return {};
  }

}// namespace

auto parse_app_config(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>
{
  AppConfig config{};
  config.ply_path = default_ply_path();

  if (args.size() <= 1) { return config; }

  size_t index = 1;
  while (index < args.size()) {
    std::string_view const arg{ bounded_arg(args, index) };

    if (arg == "--validation") {
      config.enable_validation = true;
      ++index;
      continue;
    }

    if (arg == "--no-imgui") {
      config.enable_imgui = false;
      ++index;
      continue;
    }

    if (arg == "--gpu-timers") {
      config.enable_gpu_timers = true;
      ++index;
      continue;
    }

    if (arg == "--ply") {
      if (auto parsed = parse_ply(args, index, config); !parsed) { return std::unexpected{ parsed.error() }; }
      continue;
    }

    if (arg.starts_with("--")) {
      print_usage(args.front());
      return std::unexpected{ make_error(std::errc::invalid_argument, "unknown option: " + std::string{ arg }) };
    }

    if (auto parsed = parse_shorthand_ply(args, index, config); !parsed) { return std::unexpected{ parsed.error() }; }
  }

  return config;
}

}// namespace vkgsplat::app
