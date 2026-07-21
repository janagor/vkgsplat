#pragma once

#include <expected>
#include <span>
#include <string>

#include <vkgsplat/error.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat/types.hpp>

namespace vkgsplat {

struct AppConfig
{
  SplatSource source = SplatSource::Procedural;
  u32 splat_count = 64;
  std::string ply_path;
};

[[nodiscard]] auto parse_app_config(std::span<char *const> args) -> std::expected<AppConfig, Error>;

}// namespace vkgsplat
