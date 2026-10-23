#include <vkgsplat_animation/camera_path.hpp>

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <glm/common.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_double.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/vector_double3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace vkgsplat::animation {
namespace {

  constexpr f64 kHermiteCubicCoefficient = 2.0;
  constexpr f64 kHermiteQuadraticCoefficient = 3.0;

  [[nodiscard]] auto Invalid(std::string message) -> Error
  { return MakeError(std::errc::invalid_argument, std::move(message)); }

  [[nodiscard]] auto Finite(glm::dvec3 const &value) -> bool
  {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  }

  [[nodiscard]] auto Finite(glm::dquat const &value) -> bool
  { return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

  [[nodiscard]] auto Tangent(std::vector<CameraKeyframe> const &keys, std::size_t index) -> glm::dvec3
  {
    auto const before = index == 0U ? index : index - 1U;
    auto const after = index + 1U == keys.size() ? index : index + 1U;
    return (keys.at(after).pose.position - keys.at(before).pose.position)
           / (keys.at(after).time_seconds - keys.at(before).time_seconds);
  }

  [[nodiscard]] auto InterpolatePosition(std::vector<CameraKeyframe> const &keys, std::size_t left, f64 amount)
    -> glm::dvec3
  {
    auto const &first = keys.at(left);
    auto const &last = keys.at(left + 1U);
    if (keys.size() == 2U) { return glm::mix(first.pose.position, last.pose.position, amount); }
    auto const span = last.time_seconds - first.time_seconds;
    auto const squared = amount * amount;
    auto const cubed = squared * amount;
    return ((kHermiteCubicCoefficient * cubed) - (kHermiteQuadraticCoefficient * squared) + 1.0) * first.pose.position
           + (cubed - (kHermiteCubicCoefficient * squared) + amount) * span * Tangent(keys, left)
           + ((-kHermiteCubicCoefficient * cubed) + (kHermiteQuadraticCoefficient * squared)) * last.pose.position
           + (cubed - squared) * span * Tangent(keys, left + 1U);
  }

}// namespace

CameraPath::CameraPath(std::vector<CameraKeyframe> keyframes) : keyframes_(std::move(keyframes)) {}

auto CameraPath::create(std::vector<CameraKeyframe> keyframes) -> std::expected<CameraPath, Error>
{
  if (keyframes.size() < 2U) { return std::unexpected(Invalid("keyframes: at least two are required")); }
  for (std::size_t index = 0U; index < keyframes.size(); ++index) {
    auto &key = keyframes.at(index);
    auto const prefix = "keyframes[" + std::to_string(index) + "].";
    if (!std::isfinite(key.time_seconds)
        || (index == 0U ? key.time_seconds != 0.0 : key.time_seconds <= keyframes.at(index - 1U).time_seconds)) {
      return std::unexpected(Invalid(prefix + "time: timestamps must start at zero and strictly increase"));
    }
    if (!Finite(key.pose.position)) { return std::unexpected(Invalid(prefix + "position: values must be finite")); }
    if (!Finite(key.pose.orientation)) {
      return std::unexpected(Invalid(prefix + "orientation: values must be finite"));
    }
    auto const &orientation = key.pose.orientation;
    auto const scale =
      std::max({ std::abs(orientation.w), std::abs(orientation.x), std::abs(orientation.y), std::abs(orientation.z) });
    if (scale == 0.0) { return std::unexpected(Invalid(prefix + "orientation: quaternion must be normalizable")); }
    key.pose.orientation = glm::normalize(key.pose.orientation / scale);
    if (!std::isfinite(key.pose.fov_degrees) || key.pose.fov_degrees < kCameraMinFovDegrees
        || key.pose.fov_degrees > kCameraMaxFovDegrees) {
      return std::unexpected(Invalid(prefix + "fov_degrees: expected a finite value from 1 to 120"));
    }
  }
  if (keyframes.size() > 2U) {
    for (std::size_t index = 0U; index < keyframes.size(); ++index) {
      if (!Finite(Tangent(keyframes, index))) {
        return std::unexpected(
          Invalid("keyframes[" + std::to_string(index) + "].position: interpolation tangent is not finite"));
      }
    }
  }
  return CameraPath{ std::move(keyframes) };
}

auto CameraPath::duration_seconds() const noexcept -> f64 { return keyframes_.back().time_seconds; }

auto CameraPath::keyframes() const noexcept -> std::vector<CameraKeyframe> const & { return keyframes_; }

auto CameraPath::sample(f64 time_seconds) const -> std::expected<CameraPose, Error>
{
  if (!std::isfinite(time_seconds)) { return std::unexpected(Invalid("sample time: value must be finite")); }
  if (time_seconds <= 0.0) { return keyframes_.front().pose; }
  if (time_seconds >= duration_seconds()) { return keyframes_.back().pose; }
  auto const upper = std::ranges::upper_bound(keyframes_, time_seconds, {}, &CameraKeyframe::time_seconds);
  auto const left = static_cast<std::size_t>(std::distance(keyframes_.begin(), upper) - 1);
  auto const &first = keyframes_.at(left);
  if (time_seconds == first.time_seconds) { return first.pose; }
  auto const &last = keyframes_.at(left + 1U);
  auto const amount = (time_seconds - first.time_seconds) / (last.time_seconds - first.time_seconds);
  auto end_orientation = last.pose.orientation;
  if (glm::dot(first.pose.orientation, end_orientation) < 0.0) { end_orientation = -end_orientation; }
  CameraPose pose{ .position = InterpolatePosition(keyframes_, left, amount),
    .orientation = glm::normalize(glm::slerp(first.pose.orientation, end_orientation, amount)),
    .fov_degrees = std::lerp(first.pose.fov_degrees, last.pose.fov_degrees, amount) };
  if (!Finite(pose.position) || !Finite(pose.orientation) || !std::isfinite(pose.fov_degrees)) {
    return std::unexpected(Invalid("sample time: interpolated camera pose is not finite"));
  }
  return pose;
}

auto FrameTimeSeconds(u64 index, u32 fps_numerator, u32 fps_denominator) -> std::expected<f64, Error>
{
  if (fps_numerator == 0U || fps_denominator == 0U) {
    return std::unexpected(Invalid("frame rate: numerator and denominator must be positive"));
  }
  // NOLINTBEGIN(google-runtime-float): extra precision keeps rational frame times stable at decimal boundaries.
  auto const seconds = static_cast<long double>(index) * static_cast<long double>(fps_denominator)
                       / static_cast<long double>(fps_numerator);
  if (seconds > static_cast<long double>(std::numeric_limits<f64>::max())) {
    return std::unexpected(Invalid("frame time: value exceeds floating point range"));
  }
  // NOLINTEND(google-runtime-float)
  return static_cast<f64>(seconds);
}

auto FrameCount(CameraPath const &path, u32 fps_numerator, u32 fps_denominator) -> std::expected<u64, Error>
{
  if (fps_numerator == 0U || fps_denominator == 0U) {
    return std::unexpected(Invalid("frame rate: numerator and denominator must be positive"));
  }
  auto const last_index = std::numeric_limits<u64>::max();
  auto last_time = FrameTimeSeconds(last_index, fps_numerator, fps_denominator);
  if (!last_time || *last_time < path.duration_seconds()) {
    return std::unexpected(Invalid("frame count: path duration exceeds supported range"));
  }
  // Use the same rounded timestamps as rendering so decimal durations on a frame boundary do not gain a frame.
  u64 first = 0U;
  u64 last = last_index;
  while (first < last) {
    auto const middle = first + ((last - first) / 2U);
    auto time = FrameTimeSeconds(middle, fps_numerator, fps_denominator);
    if (!time) { return std::unexpected(time.error()); }
    if (*time < path.duration_seconds()) {
      first = middle + 1U;
    } else {
      last = middle;
    }
  }
  return first;
}

}// namespace vkgsplat::animation
