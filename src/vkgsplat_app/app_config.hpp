#ifndef VKGSPLAT_APP_APP_CONFIG_HPP
#define VKGSPLAT_APP_APP_CONFIG_HPP

#include <expected>
#include <span>
#include <string>

#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::app {

inline constexpr vkgsplat::u32 kDefaultSplatCount = 64;

struct AppConfig
{
  vkgsplat::u32 splat_count = kDefaultSplatCount;
  std::string ply_path;
  bool enable_validation = false;
  bool enable_imgui = true;
  bool enable_gpu_timers = false;
  vkgsplat::FrameRateConfig frame_rate{};
};

[[nodiscard]] auto ParseAppConfig(std::span<char *const> args) -> std::expected<AppConfig, vkgsplat::Error>;

}// namespace vkgsplat::app

#endif// VKGSPLAT_APP_APP_CONFIG_HPP
