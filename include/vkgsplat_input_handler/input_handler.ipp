#ifndef VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_IPP
#define VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_IPP

#include <vkgsplat_input_handler/input_handler.hpp>// NOLINT(misc-header-include-cycle)

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace vkgsplat {

template<class View, class CloseTarget>
  requires KeyboardControllable<View> && MouseLookControllable<View> && ScrollZoomable<View> && Closeable<CloseTarget>
void InputHandler::update(View &view, CloseTarget &close)
{
  if (window_ == nullptr) { return; }

  auto const now = glfwGetTime();
  auto const delta_time = static_cast<f64>(now - last_time_);
  last_time_ = now;

  auto const key_pressed = [this](int key) -> bool { return glfwGetKey(window_, key) == GLFW_PRESS; };

  if (key_pressed(GLFW_KEY_W)) { view.process_keyboard(ViewMovement::kForward, delta_time); }
  if (key_pressed(GLFW_KEY_S)) { view.process_keyboard(ViewMovement::kBackward, delta_time); }
  if (key_pressed(GLFW_KEY_A)) { view.process_keyboard(ViewMovement::kLeft, delta_time); }
  if (key_pressed(GLFW_KEY_D)) { view.process_keyboard(ViewMovement::kRight, delta_time); }
  if (key_pressed(GLFW_KEY_SPACE)) { view.process_keyboard(ViewMovement::kUp, delta_time); }
  if (key_pressed(GLFW_KEY_LEFT_CONTROL)) { view.process_keyboard(ViewMovement::kDown, delta_time); }
  if (key_pressed(GLFW_KEY_Q)) { view.process_keyboard(ViewMovement::kRollLeft, delta_time); }
  if (key_pressed(GLFW_KEY_E)) { view.process_keyboard(ViewMovement::kRollRight, delta_time); }

  f64 cursor_x = 0.0;
  f64 cursor_y = 0.0;
  glfwGetCursorPos(window_, &cursor_x, &cursor_y);

  if (first_mouse_) {
    last_cursor_x_ = cursor_x;
    last_cursor_y_ = cursor_y;
    first_mouse_ = false;
  } else {
    auto const x_offset = cursor_x - last_cursor_x_;
    auto const y_offset = last_cursor_y_ - cursor_y;
    last_cursor_x_ = cursor_x;
    last_cursor_y_ = cursor_y;

    if (x_offset != 0.0 || y_offset != 0.0) { view.process_mouse_movement(x_offset, y_offset); }
  }

  if (scroll_y_ != 0.0) {
    view.process_mouse_scroll(scroll_y_);
    scroll_y_ = 0.0;
  }

  if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) { close.request_close(); }
}

}// namespace vkgsplat

#endif// VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_IPP
