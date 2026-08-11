#ifndef VKGSPLAT_CAMERA_HPP
#define VKGSPLAT_CAMERA_HPP

#define GLM_ENABLE_EXPERIMENTAL

#include <algorithm>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/fwd.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <vkgsplat_utility/input_control.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

using CameraMovement = ViewMovement;

constexpr f64 kCameraSpeed = 2.5;
constexpr f64 kCameraSensitivity = 0.1;
constexpr f64 kCameraDefaultFovDegrees = 45.0;
constexpr f64 kCameraMinFovDegrees = 1.0;
constexpr f64 kCameraMaxFovDegrees = 120.0;
constexpr f64 kCameraDefaultNearPlane = 0.1;
constexpr f64 kCameraDefaultFarPlane = 100.0;
constexpr f64 kCameraDefaultAspectRatio = 1.0;

inline constexpr glm::dvec3 kDefaultCameraPosition{ 0.0, 1.5, 10.0 };
inline constexpr glm::dvec3 kDefaultCameraTarget{ 0.0, 0.0, 0.0 };
inline constexpr glm::dvec3 kDefaultCameraUp{ 0.0, 1.0, 0.0 };

// Deprecated aliases (prefer kCamera*Fov*).
constexpr f64 kCameraDefaultZoom = kCameraDefaultFovDegrees;
constexpr f64 kCameraMinZoom = kCameraMinFovDegrees;
constexpr f64 kCameraMaxZoom = kCameraMaxFovDegrees;
constexpr float kCameraNearPlane = static_cast<float>(kCameraDefaultNearPlane);
constexpr float kCameraFarPlane = static_cast<float>(kCameraDefaultFarPlane);

struct CameraConfig
{
  // Extrinsics (orientation from look-at: position → target, with `up`).
  glm::dvec3 position{ kDefaultCameraPosition };
  glm::dvec3 target{ kDefaultCameraTarget };
  glm::dvec3 up{ kDefaultCameraUp };

  // Intrinsics. Live windowed rendering overrides aspect from the swapchain.
  f64 fov_degrees{ kCameraDefaultFovDegrees };
  f64 aspect_ratio{ kCameraDefaultAspectRatio };
  f64 near_plane{ kCameraDefaultNearPlane };
  f64 far_plane{ kCameraDefaultFarPlane };

  // Interaction
  f64 movement_speed{ kCameraSpeed };
  f64 mouse_sensitivity{ kCameraSensitivity };
};

struct CameraPushConstants
{
  glm::mat4 view{};
  glm::mat4 projection{};
};

constexpr auto kCameraPushPositionSize = 128;
static_assert(sizeof(CameraPushConstants) == kCameraPushPositionSize);

class Camera
{
public:
  [[nodiscard]] auto position() const noexcept -> glm::dvec3 const & { return position_; }
  [[nodiscard]] auto orientation() const noexcept -> glm::dquat const & { return orientation_; }
  [[nodiscard]] auto front() const noexcept -> glm::dvec3 const & { return front_; }
  [[nodiscard]] auto up() const noexcept -> glm::dvec3 const & { return up_; }
  [[nodiscard]] auto right() const noexcept -> glm::dvec3 const & { return right_; }

  [[nodiscard]] auto movement_speed() const noexcept -> f64 { return movement_speed_; }
  [[nodiscard]] auto mouse_sensitivity() const noexcept -> f64 { return mouse_sensitivity_; }
  [[nodiscard]] auto zoom() const noexcept -> f64 { return fov_degrees_; }
  [[nodiscard]] auto fov_degrees() const noexcept -> f64 { return fov_degrees_; }
  [[nodiscard]] auto near_plane() const noexcept -> f64 { return near_plane_; }
  [[nodiscard]] auto far_plane() const noexcept -> f64 { return far_plane_; }
  [[nodiscard]] auto aspect_ratio() const noexcept -> f64 { return aspect_ratio_; }

  explicit Camera(CameraConfig const &config = {})
    : position_(config.position), movement_speed_(config.movement_speed),
      mouse_sensitivity_(config.mouse_sensitivity), fov_degrees_(config.fov_degrees),
      near_plane_(config.near_plane), far_plane_(config.far_plane), aspect_ratio_(config.aspect_ratio)
  {
    auto const world_up =
      glm::length(config.up) > 0.0 ? glm::normalize(config.up) : kDefaultCameraUp;
    auto const look_at = glm::lookAt(config.position, config.target, world_up);
    orientation_ = glm::conjugate(glm::quat_cast(look_at));
    update_camera_vectors();
  }

  explicit Camera(glm::dvec3 position, glm::dvec3 target)
    : Camera(CameraConfig{ .position = position, .target = target })
  {}

  [[nodiscard]] auto view_matrix() const noexcept -> glm::mat4
  {
    auto const rotate = glm::mat4_cast(glm::conjugate(orientation_));
    auto const translate = glm::translate(glm::dmat4(1.0), -position_);
    return rotate * translate;
  }

  // `aspect` overrides the configured aspect (typical for swapchain frames).
  [[nodiscard]] auto projection_matrix(f64 aspect) const noexcept -> glm::mat4
  {
    auto const used_aspect = aspect > 0.0 ? aspect : aspect_ratio_;
    auto proj = glm::perspective(glm::radians(static_cast<float>(fov_degrees_)),
      static_cast<float>(used_aspect),
      static_cast<float>(near_plane_),
      static_cast<float>(far_plane_));
    proj[1][1] *= -1.0F;// NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return proj;
  }

  [[nodiscard]] auto projection_matrix() const noexcept -> glm::mat4 { return projection_matrix(aspect_ratio_); }

  void process_keyboard(ViewMovement direction, f64 delta_time)
  {
    auto const velocity = movement_speed_ * delta_time;

    if (direction == ViewMovement::kForward) { position_ += front_ * velocity; }
    if (direction == ViewMovement::kBackward) { position_ -= front_ * velocity; }
    if (direction == ViewMovement::kLeft) { position_ -= right_ * velocity; }
    if (direction == ViewMovement::kRight) { position_ += right_ * velocity; }
    if (direction == ViewMovement::kUp) { position_ += up_ * velocity; }
    if (direction == ViewMovement::kDown) { position_ -= up_ * velocity; }

    if (direction == ViewMovement::kRollLeft) {
      auto const q_roll = glm::angleAxis(glm::radians(-50.0 * delta_time), front_);
      orientation_ = q_roll * orientation_;
      orientation_ = glm::normalize(orientation_);
      update_camera_vectors();
    }
    if (direction == ViewMovement::kRollRight) {
      auto const q_roll = glm::angleAxis(glm::radians(50.0 * delta_time), front_);
      orientation_ = q_roll * orientation_;
      orientation_ = glm::normalize(orientation_);
      update_camera_vectors();
    }
  }

  void process_mouse_movement(f64 xoffset, f64 yoffset)
  {
    auto const yaw_amount = xoffset * mouse_sensitivity_;
    auto const pitch_amount = yoffset * mouse_sensitivity_;

    auto const q_yaw = glm::angleAxis(glm::radians(-yaw_amount), up_);
    // Input already uses screen-up as +y (last_y - cursor_y); positive pitch looks up.
    auto const q_pitch = glm::angleAxis(glm::radians(pitch_amount), right_);

    orientation_ = q_yaw * q_pitch * orientation_;
    orientation_ = glm::normalize(orientation_);

    update_camera_vectors();
  }

  void process_mouse_scroll(f64 yoffset)
  {
    fov_degrees_ -= yoffset;
    fov_degrees_ = std::clamp(fov_degrees_, kCameraMinFovDegrees, kCameraMaxFovDegrees);
  }

private:
  void update_camera_vectors()
  {
    front_ = glm::normalize(orientation_ * glm::dvec3(0.0, 0.0, -1.0));
    right_ = glm::normalize(orientation_ * glm::dvec3(1.0, 0.0, 0.0));
    up_ = glm::normalize(orientation_ * glm::dvec3(0.0, 1.0, 0.0));
  }

  glm::dvec3 position_{};
  glm::dquat orientation_{};

  glm::dvec3 front_{};
  glm::dvec3 up_{};
  glm::dvec3 right_{};

  f64 movement_speed_{ kCameraSpeed };
  f64 mouse_sensitivity_{ kCameraSensitivity };
  f64 fov_degrees_{ kCameraDefaultFovDegrees };
  f64 near_plane_{ kCameraDefaultNearPlane };
  f64 far_plane_{ kCameraDefaultFarPlane };
  f64 aspect_ratio_{ kCameraDefaultAspectRatio };
};

static_assert(KeyboardControllable<Camera>);
static_assert(MouseLookControllable<Camera>);
static_assert(ScrollZoomable<Camera>);

}// namespace vkgsplat

#endif// VKGSPLAT_CAMERA_HPP
