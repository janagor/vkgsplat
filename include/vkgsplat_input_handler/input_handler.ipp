#ifndef VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_IPP
#define VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_IPP

#include <vkgsplat_input_handler/input_handler.hpp>// NOLINT(misc-header-include-cycle)

#include <algorithm>

#include <GLFW/glfw3.h>

namespace vkgsplat {

namespace {

  // After idle wait_events(), wall time can be huge; don't apply that as motion dt.
  constexpr f64 kMaxInputDeltaTime = 1.0 / 30.0;

}// namespace

template<class View, class CloseTarget>
  requires KeyboardControllable<View> && MouseLookControllable<View> && ScrollZoomable<View> && Closeable<CloseTarget>
auto InputHandler::update(View &view, CloseTarget &close) -> bool
{
  if (window_ == nullptr) { return false; }

  auto const now = glfwGetTime();
  auto const raw_delta_time = static_cast<f64>(now - last_time_);
  last_time_ = now;
  auto const delta_time = std::min(raw_delta_time, kMaxInputDeltaTime);
  auto const after_hitch = raw_delta_time > kMaxInputDeltaTime;

  auto const key_pressed = [this](int key) -> bool { return glfwGetKey(window_, key) == GLFW_PRESS; };

  bool view_changed = false;

  auto const apply_move = [&](int key, ViewMovement direction) -> void {
    if (!key_pressed(key)) { return; }
    view.process_keyboard(direction, delta_time);
    view_changed = true;
  };

  apply_move(GLFW_KEY_W, ViewMovement::kForward);
  apply_move(GLFW_KEY_S, ViewMovement::kBackward);
  apply_move(GLFW_KEY_A, ViewMovement::kLeft);
  apply_move(GLFW_KEY_D, ViewMovement::kRight);
  apply_move(GLFW_KEY_SPACE, ViewMovement::kUp);
  apply_move(GLFW_KEY_LEFT_CONTROL, ViewMovement::kDown);
  apply_move(GLFW_KEY_Q, ViewMovement::kRollLeft);
  apply_move(GLFW_KEY_E, ViewMovement::kRollRight);

  f64 cursor_x = 0.0;
  f64 cursor_y = 0.0;
  glfwGetCursorPos(window_, &cursor_x, &cursor_y);

  if (first_mouse_ || after_hitch) {
    // Re-baseline after startup or idle so accumulated cursor deltas aren't one huge look.
    last_cursor_x_ = cursor_x;
    last_cursor_y_ = cursor_y;
    first_mouse_ = false;
  } else {
    auto const x_offset = cursor_x - last_cursor_x_;
    auto const y_offset = last_cursor_y_ - cursor_y;
    last_cursor_x_ = cursor_x;
    last_cursor_y_ = cursor_y;

    if (x_offset != 0.0 || y_offset != 0.0) {
      view.process_mouse_movement(x_offset, y_offset);
      view_changed = true;
    }
  }

  if (scroll_y_ != 0.0) {
    view.process_mouse_scroll(scroll_y_);
    scroll_y_ = 0.0;
    view_changed = true;
  }

  if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) { close.request_close(); }

  auto const f2_pressed = key_pressed(GLFW_KEY_F2);
  if (f2_pressed && !f2_was_pressed_) { screenshot_requested_ = true; }
  f2_was_pressed_ = f2_pressed;

  auto const m_pressed = key_pressed(GLFW_KEY_M);
  if (m_pressed && !m_was_pressed_) { emulate_toggle_requested_ = true; }
  m_was_pressed_ = m_pressed;

  auto const edge_arrow = [&](int key, bool &was, ArrowDir dir) -> void {
    auto const pressed = key_pressed(key);
    if (pressed && !was) { pending_arrow_ = dir; }
    was = pressed;
  };
  edge_arrow(GLFW_KEY_LEFT, left_was_pressed_, ArrowDir::kLeft);
  edge_arrow(GLFW_KEY_RIGHT, right_was_pressed_, ArrowDir::kRight);
  edge_arrow(GLFW_KEY_UP, up_was_pressed_, ArrowDir::kUp);
  edge_arrow(GLFW_KEY_DOWN, down_was_pressed_, ArrowDir::kDown);

  return view_changed;
}

}// namespace vkgsplat

#endif// VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_IPP
