#ifndef VKGSPLAT_ANIMATION_CAMERA_PATH_HPP
#define VKGSPLAT_ANIMATION_CAMERA_PATH_HPP

#include <vkgsplat_animation/vkgsplat_animation_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <glm/ext/quaternion_double.hpp>
#include <glm/ext/vector_double3.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace vkgsplat::animation {

struct CameraPose
{
  glm::dvec3 position{};
  glm::dquat orientation{};
  f64 fov_degrees{};
};

struct CameraKeyframe
{
  f64 time_seconds{};
  CameraPose pose{};
};

class VKGSPLAT_ANIMATION_EXPORT CameraPath
{
public:
  [[nodiscard]] static auto create(std::vector<CameraKeyframe> keyframes) -> std::expected<CameraPath, Error>;

  [[nodiscard]] auto duration_seconds() const noexcept -> f64;
  [[nodiscard]] auto sample(f64 time_seconds) const -> std::expected<CameraPose, Error>;
  [[nodiscard]] auto keyframes() const noexcept -> std::vector<CameraKeyframe> const &;

private:
  explicit CameraPath(std::vector<CameraKeyframe> keyframes);

  std::vector<CameraKeyframe> keyframes_;
};

[[nodiscard]] VKGSPLAT_ANIMATION_EXPORT auto LoadCameraPath(std::filesystem::path const &path)
  -> std::expected<CameraPath, Error>;
[[nodiscard]] VKGSPLAT_ANIMATION_EXPORT auto ParseCameraPathJson(std::string const &json)
  -> std::expected<CameraPath, Error>;
[[nodiscard]] VKGSPLAT_ANIMATION_EXPORT auto FrameTimeSeconds(u64 index, u32 fps_numerator, u32 fps_denominator)
  -> std::expected<f64, Error>;
[[nodiscard]] VKGSPLAT_ANIMATION_EXPORT auto FrameCount(CameraPath const &path, u32 fps_numerator, u32 fps_denominator)
  -> std::expected<u64, Error>;

}// namespace vkgsplat::animation

#endif// VKGSPLAT_ANIMATION_CAMERA_PATH_HPP
