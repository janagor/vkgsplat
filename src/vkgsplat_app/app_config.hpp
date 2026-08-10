#ifndef VKGSPLAT_APP_APP_CONFIG_HPP
#define VKGSPLAT_APP_APP_CONFIG_HPP

#include <array>
#include <expected>
#include <span>
#include <string>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat/lfd_config.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::app {

struct AppConfig
{
  std::string ply_path;
  bool enable_validation = false;
  bool enable_imgui = true;
  bool enable_gpu_timers = false;
  vkgsplat::FrameRateConfig frame_rate{};
  vkgsplat::CameraConfig camera{};
  // LFD quilt grid; {1,1} is standard mono rendering. [columns, rows]
  std::array<u32, 2> lfd_grid{ 1U, 1U };
  // Horizontal view cone (degrees) across quilt columns; rows use aspect-scaled cone.
  vkgsplat::f64 view_cone_deg{ vkgsplat::kDefaultViewConeDegrees };
};

[[nodiscard]] auto ParseAppConfig(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>;

}// namespace vkgsplat::app

#endif// VKGSPLAT_APP_APP_CONFIG_HPP
