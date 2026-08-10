#ifndef VKGSPLAT_APP_APP_CONFIG_HPP
#define VKGSPLAT_APP_APP_CONFIG_HPP

#include <array>
#include <expected>
#include <span>
#include <string>

#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::app {

// Matches vkgsplat::kDefaultCameraPosition {0.0, 1.5, 10.0}.
inline constexpr std::array<vkgsplat::f64, 3> kDefaultCameraPosition{ 0.0, 1.5, 10.0 };
inline constexpr std::array<vkgsplat::f64, 3> kDefaultCameraTarget{ 0.0, 0.0, 0.0 };

struct AppConfig
{
  std::string ply_path;
  bool enable_validation = false;
  bool enable_imgui = true;
  bool enable_gpu_timers = false;
  vkgsplat::FrameRateConfig frame_rate{};
  std::array<vkgsplat::f64, 3> camera_position = kDefaultCameraPosition;
  std::array<vkgsplat::f64, 3> camera_target = kDefaultCameraTarget;
};

[[nodiscard]] auto ParseAppConfig(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>;

}// namespace vkgsplat::app

#endif// VKGSPLAT_APP_APP_CONFIG_HPP
