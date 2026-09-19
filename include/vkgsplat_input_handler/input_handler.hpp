#ifndef VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_HPP
#define VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_HPP

#include <vkgsplat_input_handler/key_bindings.hpp>
#include <vkgsplat_utility/input_control.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_window/window.hpp>

#include <algorithm>

#include <GLFW/glfw3.h>

struct GLFWwindow;

namespace vkgsplat {

/**
 * Polls GLFW input and applies it to a keyboard/mouse-controllable view.
 *
 * `update` returns true when the view changed. Discrete actions are exposed as
 * edge-triggered requests and remain pending until their consume function is
 * called.
 */
class InputHandler
{
public:
  explicit InputHandler(Window &window, KeyBindings bindings = {});
  ~InputHandler();

  InputHandler(InputHandler const & /*other*/) = delete;
  auto operator=(InputHandler const & /*other*/) -> InputHandler & = delete;
  InputHandler(InputHandler &&other) noexcept;
  auto operator=(InputHandler &&other) noexcept -> InputHandler &;

  template<class View, class CloseTarget>
    requires KeyboardControllable<View> && MouseLookControllable<View> && ScrollZoomable<View> && Closeable<CloseTarget>
  /** Poll input; returns true when the view changed and needs redraw. */
  [[nodiscard]] auto update(View &view, CloseTarget &close) -> bool
  {
    if (window_ == nullptr) { return false; }

    // After idle wait_events(), wall time can be huge; don't apply that as motion dt.
    constexpr f64 kMaxInputDeltaTime = 1.0 / 30.0;

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

    apply_move(bindings_.forward, ViewMovement::kForward);
    apply_move(bindings_.backward, ViewMovement::kBackward);
    apply_move(bindings_.left, ViewMovement::kLeft);
    apply_move(bindings_.right, ViewMovement::kRight);
    apply_move(bindings_.up, ViewMovement::kUp);
    apply_move(bindings_.down, ViewMovement::kDown);
    apply_move(bindings_.roll_left, ViewMovement::kRollLeft);
    apply_move(bindings_.roll_right, ViewMovement::kRollRight);

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

    if (key_pressed(bindings_.close)) { close.request_close(); }

    auto const screenshot_pressed = key_pressed(bindings_.screenshot);
    if (screenshot_pressed && !screenshot_was_pressed_) { screenshot_requested_ = true; }
    screenshot_was_pressed_ = screenshot_pressed;

    auto const emulate_pressed = key_pressed(bindings_.emulate_toggle);
    if (emulate_pressed && !emulate_was_pressed_) { emulate_toggle_requested_ = true; }
    emulate_was_pressed_ = emulate_pressed;

    auto const edge_arrow = [&](int key, bool &was, ArrowDir dir) -> void {
      auto const pressed = key_pressed(key);
      if (pressed && !was) { pending_arrow_ = dir; }
      was = pressed;
    };
    edge_arrow(bindings_.cell_left, left_was_pressed_, ArrowDir::kLeft);
    edge_arrow(bindings_.cell_right, right_was_pressed_, ArrowDir::kRight);
    edge_arrow(bindings_.cell_up, up_was_pressed_, ArrowDir::kUp);
    edge_arrow(bindings_.cell_down, down_was_pressed_, ArrowDir::kDown);

    return view_changed;
  }

  /** Return and clear one pending screenshot request. */
  [[nodiscard]] auto consume_screenshot_request() noexcept -> bool;
  [[nodiscard]] auto screenshot_requested() const noexcept -> bool { return screenshot_requested_; }

  /** Return and clear one pending quilt-emulation toggle request. */
  [[nodiscard]] auto consume_emulate_toggle() noexcept -> bool;

  enum class ArrowDir : u8 { kNone, kLeft, kRight, kUp, kDown };
  /** Return and clear one pending quilt-cell navigation direction. */
  [[nodiscard]] auto consume_arrow() noexcept -> ArrowDir;

private:
  static void scroll_callback(GLFWwindow *window, f64 /*x_offset*/, f64 y_offset);

  GLFWwindow *window_ = nullptr;
  KeyBindings bindings_{};
  f64 last_time_ = 0.0;
  f64 last_cursor_x_ = 0.0;
  f64 last_cursor_y_ = 0.0;
  f64 scroll_y_ = 0.0;
  bool first_mouse_ = true;
  bool screenshot_was_pressed_ = false;
  bool screenshot_requested_ = false;
  bool emulate_was_pressed_ = false;
  bool emulate_toggle_requested_ = false;
  bool left_was_pressed_ = false;
  bool right_was_pressed_ = false;
  bool up_was_pressed_ = false;
  bool down_was_pressed_ = false;
  ArrowDir pending_arrow_{ ArrowDir::kNone };
  void *previous_user_pointer_ = nullptr;
};

}// namespace vkgsplat

#endif// VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_HPP
