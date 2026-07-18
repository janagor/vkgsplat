#pragma once

#include <optional>
#include <span>
#include <string>

#include <vkgsplat/renderer.hpp>
#include <vkgsplat/types.hpp>

namespace vkgsplat {

struct AppConfig
{
  SplatSource source = SplatSource::Procedural;
  u32 splat_count = 64;
  std::string ply_path;
};

[[nodiscard]] auto parse_app_config(std::span<char *const> args) -> std::optional<AppConfig>;

}// namespace vkgsplat
