#pragma once

#include <expected>
#include <span>
#include <string>

#include <vkgsplat/error.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat/types.hpp>

namespace vkgsplat::app {

struct AppConfig
{
  vkgsplat::SplatSource source = vkgsplat::SplatSource::Procedural;
  vkgsplat::u32 splat_count = 64;
  std::string ply_path;
};

[[nodiscard]] auto parse_app_config(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>;

}// namespace vkgsplat::app
