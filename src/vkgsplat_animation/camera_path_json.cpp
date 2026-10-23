#include <vkgsplat_animation/camera_path.hpp>

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_double.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace vkgsplat::animation {
namespace {

constexpr int kCameraPathVersion = 1;
constexpr std::size_t kVectorComponents = 3U;
constexpr std::size_t kQuaternionComponents = 4U;
using Json = nlohmann::json;// NOLINT(misc-include-cleaner): nlohmann/json.hpp is included directly above.

[[nodiscard]] auto Invalid(std::string message) -> Error
{ return MakeError(std::errc::invalid_argument, std::move(message)); }

[[nodiscard]] auto Number(Json const &object, char const *field, std::string const &prefix)
  -> std::expected<f64, Error>
{
  if (!object.contains(field) || !object.at(field).is_number()) {
    return std::unexpected(Invalid(prefix + field + ": expected a number"));
  }
  auto const value = object.at(field).get<f64>();
  if (!std::isfinite(value)) { return std::unexpected(Invalid(prefix + field + ": value must be finite")); }
  return value;
}

template<std::size_t Count>
[[nodiscard]] auto Components(Json const &object, char const *field, std::string const &prefix)
  -> std::expected<std::array<f64, Count>, Error>
{
  if (!object.contains(field) || !object.at(field).is_array() || object.at(field).size() != Count) {
    return std::unexpected(Invalid(prefix + field + ": expected an array of " + std::to_string(Count) + " numbers"));
  }
  std::array<f64, Count> values{};
  for (std::size_t index = 0U; index < Count; ++index) {
    auto const &item = object.at(field).at(index);
    if (!item.is_number()) { return std::unexpected(Invalid(prefix + field + ": expected finite numbers")); }
    values.at(index) = item.get<f64>();
    if (!std::isfinite(values.at(index))) {
      return std::unexpected(Invalid(prefix + field + ": expected finite numbers"));
    }
  }
  return values;
}

[[nodiscard]] auto Vector(Json const &object, char const *field, std::string const &prefix)
  -> std::expected<glm::dvec3, Error>
{
  auto values = Components<kVectorComponents>(object, field, prefix);
  if (!values) { return std::unexpected(values.error()); }
  return glm::dvec3{ values->at(0), values->at(1), values->at(2) };
}

[[nodiscard]] auto ParseKeyframe(Json const &item, std::size_t index)
  -> std::expected<CameraKeyframe, Error>
{
  auto const prefix = "keyframes[" + std::to_string(index) + "].";
  if (!item.is_object()) { return std::unexpected(Invalid(prefix + ": expected an object")); }
  auto time = Number(item, "time", prefix);
  if (!time) { return std::unexpected(time.error()); }
  auto position = Vector(item, "position", prefix);
  if (!position) { return std::unexpected(position.error()); }
  auto fov = Number(item, "fov_degrees", prefix);
  if (!fov) { return std::unexpected(fov.error()); }
  if (item.contains("orientation") == item.contains("target")) {
    return std::unexpected(Invalid(prefix + "orientation/target: specify exactly one"));
  }
  glm::dquat orientation{};
  if (item.contains("orientation")) {
    if (item.contains("up")) { return std::unexpected(Invalid(prefix + "up: only valid with target")); }
    auto values = Components<kQuaternionComponents>(item, "orientation", prefix);
    if (!values) { return std::unexpected(values.error()); }
    orientation = glm::dquat{ values->at(3), values->at(0), values->at(1), values->at(2) };
  } else {
    auto target = Vector(item, "target", prefix);
    if (!target) { return std::unexpected(target.error()); }
    auto world_up = glm::dvec3{ kDefaultCameraUp };
    if (item.contains("up")) {
      auto supplied = Vector(item, "up", prefix);
      if (!supplied) { return std::unexpected(supplied.error()); }
      world_up = *supplied;
    }
    auto const forward = *target - *position;
    if (!std::isfinite(glm::length(forward)) || glm::length(forward) == 0.0) {
      return std::unexpected(Invalid(prefix + "target: must differ from position"));
    }
    auto const right = glm::cross(forward, world_up);
    if (!std::isfinite(glm::length(right)) || glm::length(right) == 0.0) {
      return std::unexpected(Invalid(prefix + "up: must not be zero or parallel to view direction"));
    }
    orientation = glm::conjugate(glm::quat_cast(glm::lookAt(*position, *target, glm::normalize(world_up))));
  }
  return CameraKeyframe{ .time_seconds = *time,
    .pose = { .position = *position, .orientation = orientation, .fov_degrees = *fov } };
}

}// namespace

auto ParseCameraPathJson(std::string const &json) -> std::expected<CameraPath, Error>
{
  try {
    auto document = Json::parse(json, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
      return std::unexpected(Invalid("camera path: invalid JSON object"));
    }
    if (!document.contains("version") || !document.at("version").is_number_integer()
        || document.at("version") != kCameraPathVersion) {
      return std::unexpected(Invalid("version: expected supported camera path version 1"));
    }
    if (!document.contains("keyframes") || !document.at("keyframes").is_array()) {
      return std::unexpected(Invalid("keyframes: expected an array"));
    }
    std::vector<CameraKeyframe> keyframes;
    for (auto const &item : document.at("keyframes")) {
      auto keyframe = ParseKeyframe(item, keyframes.size());
      if (!keyframe) { return std::unexpected(keyframe.error()); }
      keyframes.push_back(*keyframe);
    }
    return CameraPath::create(std::move(keyframes));
  } catch (Json::exception const &error) {
    return std::unexpected(Invalid("camera path JSON: " + std::string{ error.what() }));
  }
}

auto LoadCameraPath(std::filesystem::path const &path) -> std::expected<CameraPath, Error>
{
  errno = 0;
  std::ifstream input{ path };
  if (!input) {
    auto const code = errno == 0 ? std::make_error_code(std::errc::io_error)
                                 : std::error_code{ errno, std::generic_category() };
    return std::unexpected(MakeError(code, "camera path: cannot open " + path.string()));
  }
  auto const contents = std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
  if (input.bad()) { return std::unexpected(MakeError(std::errc::io_error, "camera path: failed to read " + path.string())); }
  return ParseCameraPathJson(contents);
}

}// namespace vkgsplat::animation
