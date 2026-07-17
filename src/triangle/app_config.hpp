#pragma once

#include <optional>
#include <span>
#include <string>

#include "types.hpp"

namespace vkgsplat {

enum class SplatSource : u8
{
  Procedural,
  Ply,
};

struct AppConfig
{
  SplatSource source = SplatSource::Procedural;
  u32 splat_count = 64;
  std::string ply_path;
};

[[nodiscard]] auto next_power_of_2(u32 value) -> u32;

[[nodiscard]] auto parse_app_config(std::span<char *const> args) -> std::optional<AppConfig>;

}// namespace vkgsplat
