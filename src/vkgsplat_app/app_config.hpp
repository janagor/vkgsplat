#pragma once

#include <expected>
#include <span>
#include <string>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::app {

struct AppConfig
{
  vkgsplat::u32 splat_count = 64;
  std::string ply_path;
  bool enable_validation = false;
  bool enable_imgui = true;
  bool enable_gpu_timers = false;
};

[[nodiscard]] auto parse_app_config(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>;

}// namespace vkgsplat::app
