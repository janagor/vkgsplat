#pragma once

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
constexpr f64 kCameraDefaultZoom = 45.0;
constexpr f64 kCameraMinZoom = 1.0;
constexpr f64 kCameraMaxZoom = 45.0;

inline constexpr glm::dvec3 kDefaultCameraPosition{ 0.0, 1.5, 10.0 };

struct CameraPushConstants
{
  glm::mat4 view{};
  glm::mat4 projection{};
};

static_assert(sizeof(CameraPushConstants) == 128);

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
  [[nodiscard]] auto zoom() const noexcept -> f64 { return zoom_; }

  explicit Camera(glm::dvec3 position = glm::dvec3(0.0, 0.0, 0.0)) : position_(position)
  {
    auto const look_at = glm::lookAt(position, glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(0.0, 1.0, 0.0));
    orientation_ = glm::conjugate(glm::quat_cast(look_at));
    update_camera_vectors();
  }

  [[nodiscard]] auto view_matrix() const noexcept -> glm::mat4
  {
    auto const rotate = glm::mat4_cast(glm::conjugate(orientation_));
    auto const translate = glm::translate(glm::dmat4(1.0), -position_);
    return rotate * translate;
  }

  [[nodiscard]] auto projection_matrix(f64 aspect_ratio) const noexcept -> glm::mat4
  {
    auto proj =
      glm::perspective(glm::radians(static_cast<float>(zoom_)), static_cast<float>(aspect_ratio), 0.1F, 100.0F);
    proj[1][1] *= -1.0F;
    return proj;
  }

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
    auto const q_pitch = glm::angleAxis(glm::radians(-pitch_amount), right_);

    orientation_ = q_yaw * q_pitch * orientation_;
    orientation_ = glm::normalize(orientation_);

    update_camera_vectors();
  }

  void process_mouse_scroll(f64 yoffset)
  {
    zoom_ -= yoffset;
    zoom_ = std::clamp(zoom_, kCameraMinZoom, kCameraMaxZoom);
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
  f64 zoom_{ kCameraDefaultZoom };
};

static_assert(KeyboardControllable<Camera>);
static_assert(MouseLookControllable<Camera>);
static_assert(ScrollZoomable<Camera>);

}// namespace vkgsplat
