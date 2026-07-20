#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include <algorithm>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/fwd.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <vkgsplat/types.hpp>

namespace vkgsplat {

enum class CameraMovement : u8 {
  Forward,
  Backward,
  Left,
  Right,
  Up,
  Down,
  RollLeft,
  RollRight,
};

constexpr f64 k_camera_speed = 2.5;
constexpr f64 k_camera_sensitivity = 0.1;
constexpr f64 k_camera_default_zoom = 45.0;
constexpr f64 k_camera_min_zoom = 1.0;
constexpr f64 k_camera_max_zoom = 45.0;

inline constexpr glm::dvec3 k_default_camera_position{ 0.0, 1.5, 10.0 };

struct CameraPushConstants
{
  glm::mat4 view{};
  glm::mat4 projection{};
};

static_assert(sizeof(CameraPushConstants) == 128);

// Push data for Stage 1 projection compute (view/proj + viewport in pixels).
struct ProjectPushConstants
{
  glm::mat4 view{};
  glm::mat4 projection{};
  glm::vec2 viewport{};// width, height
  glm::vec2 padding{};
};

static_assert(sizeof(ProjectPushConstants) == 144);

// Push data for Stage 2 tile binning.
struct BinPushConstants
{
  glm::uvec2 viewport{};// width, height in pixels
  u32 max_instances{};
  u32 tile_size{ 16 };
  u64 instance_count_address{};// BDA for atomic counter (Mesa heap atomics are broken)
};

static_assert(sizeof(BinPushConstants) == 24);

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
    auto const look_at = glm::lookAt(position, glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0));
    orientation_ = glm::conjugate(glm::quat_cast(look_at));
    update_camera_vectors();
  }

  [[nodiscard]] auto view_matrix() const -> glm::mat4
  {
    auto const rotate = glm::mat4_cast(glm::conjugate(orientation_));
    auto const translate = glm::translate(glm::dmat4(1.0), -position_);
    return rotate * translate;
  }

  [[nodiscard]] auto projection_matrix(f64 aspect_ratio) const -> glm::mat4
  {
    auto proj =
      glm::perspective(glm::radians(static_cast<float>(zoom_)), static_cast<float>(aspect_ratio), 0.1F, 100.0F);
    proj[1][1] *= -1.0F;
    return proj;
  }

  void process_keyboard(CameraMovement direction, f64 delta_time)
  {
    auto const velocity = movement_speed_ * delta_time;

    if (direction == CameraMovement::Forward) { position_ += front_ * velocity; }
    if (direction == CameraMovement::Backward) { position_ -= front_ * velocity; }
    if (direction == CameraMovement::Left) { position_ -= right_ * velocity; }
    if (direction == CameraMovement::Right) { position_ += right_ * velocity; }
    if (direction == CameraMovement::Up) { position_ += up_ * velocity; }
    if (direction == CameraMovement::Down) { position_ -= up_ * velocity; }

    if (direction == CameraMovement::RollLeft) {
      auto const q_roll = glm::angleAxis(glm::radians(-50.0 * delta_time), front_);
      orientation_ = q_roll * orientation_;
      orientation_ = glm::normalize(orientation_);
      update_camera_vectors();
    }
    if (direction == CameraMovement::RollRight) {
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
    zoom_ = std::clamp(zoom_, k_camera_min_zoom, k_camera_max_zoom);
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

  f64 movement_speed_{ k_camera_speed };
  f64 mouse_sensitivity_{ k_camera_sensitivity };
  f64 zoom_{ k_camera_default_zoom };
};

}// namespace vkgsplat
