#include "app_config.hpp"

#include <vkgsplat/renderer.hpp>
#include <vkgsplat/types.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>

#ifndef VKGSPLAT_SOURCE_DIR
#define VKGSPLAT_SOURCE_DIR "."
#endif

namespace vkgsplat {

namespace {

  [[nodiscard]] auto default_ply_path() -> std::string
  { return std::string{ VKGSPLAT_SOURCE_DIR } + "/resources/scene.ply"; }

  [[nodiscard]] auto is_unsigned_integer(std::string_view text) -> bool
  {
    if (text.empty()) { return false; }

    return std::ranges::all_of(
      text, [](char const character) { return std::isdigit(static_cast<unsigned char>(character)) != 0; });
  }

  [[nodiscard]] auto parse_count(std::string_view text) -> std::optional<u32>
  {
    if (!is_unsigned_integer(text)) { return std::nullopt; }

    try {
      unsigned long const parsed = std::stoul(std::string{ text });
      if (parsed == 0UL) { return std::nullopt; }
      return static_cast<u32>(parsed);
    } catch (std::exception const &) {
      return std::nullopt;
    }
  }

  void print_usage(std::string_view program_name)
  {
    std::println(stderr, "Usage:");
    std::println(stderr, "  {} [--procedural <count>]", program_name);
    std::println(stderr, "  {} --ply <count> [ply_path]", program_name);
    std::println(stderr, "  {} <count> [ply_path]", program_name);
    std::println(stderr, "");
    std::println(stderr, "  --procedural  generate random spheres (default: 64)");
    std::println(stderr, "  --ply         load first N splats from a PLY file");
    std::println(stderr, "  <count>       shorthand for --ply <count>");
  }

  [[nodiscard]] auto bounded_arg(std::span<char *const> args, size_t index) -> char *
  { return args.subspan(index, 1).front(); }

  [[nodiscard]] auto require_count(std::span<char *const> args, size_t &index) -> std::optional<u32>
  {
    if (index >= args.size()) {
      print_usage(args.front());
      return std::nullopt;
    }

    auto const count = parse_count(bounded_arg(args, index));
    if (!count) {
      print_usage(args.front());
      return std::nullopt;
    }

    ++index;
    return count;
  }

  [[nodiscard]] auto parse_procedural(std::span<char *const> args, size_t &index, AppConfig &config) -> bool
  {
    config.source = SplatSource::Procedural;
    ++index;

    auto const count = require_count(args, index);
    if (!count) { return false; }

    config.splat_count = *count;
    return true;
  }

  [[nodiscard]] auto parse_ply(std::span<char *const> args, size_t &index, AppConfig &config) -> bool
  {
    config.source = SplatSource::Ply;
    ++index;

    auto const count = require_count(args, index);
    if (!count) { return false; }

    config.splat_count = *count;

    if (index < args.size() && !std::string_view{ bounded_arg(args, index) }.starts_with("--")) {
      config.ply_path = bounded_arg(args, index);
      ++index;
    }

    return true;
  }

  [[nodiscard]] auto parse_shorthand_ply(std::span<char *const> args, size_t &index, AppConfig &config) -> bool
  {
    auto const count = parse_count(bounded_arg(args, index));
    if (!count) {
      print_usage(args.front());
      return false;
    }

    config.source = SplatSource::Ply;
    config.splat_count = *count;
    ++index;

    if (index < args.size()) {
      config.ply_path = bounded_arg(args, index);
      ++index;
    }

    if (index < args.size()) {
      print_usage(args.front());
      return false;
    }

    return true;
  }

}// namespace

auto parse_app_config(std::span<char *const> args) -> std::optional<AppConfig>
{
  AppConfig config{};
  config.ply_path = default_ply_path();

  if (args.size() <= 1) { return config; }

  size_t index = 1;
  while (index < args.size()) {
    std::string_view const arg{ bounded_arg(args, index) };

    if (arg == "--procedural" || arg == "--random") {
      if (!parse_procedural(args, index, config)) { return std::nullopt; }
      continue;
    }

    if (arg == "--ply") {
      if (!parse_ply(args, index, config)) { return std::nullopt; }
      continue;
    }

    if (arg.starts_with("--")) {
      print_usage(args.front());
      return std::nullopt;
    }

    if (!parse_shorthand_ply(args, index, config)) { return std::nullopt; }
    return config;
  }

  return config;
}

}// namespace vkgsplat
