#include <vkgsplat_input_handler/input_handler.hpp>
#include <vkgsplat_input_handler/key_bindings.hpp>
#include <vkgsplat_input_handler/movement_clock.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_window/window.hpp>

#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <utility>

namespace vkgsplat {

void InputHandler::scroll_callback(GLFWwindow *window, f64 /*x_offset*/, f64 y_offset)
{
  auto *const handler = static_cast<InputHandler *>(glfwGetWindowUserPointer(window));
  if (handler == nullptr) { return; }
  handler->scroll_y_ += y_offset;
}

auto InputHandler::key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) -> void
{
  auto *const handler = static_cast<InputHandler *>(glfwGetWindowUserPointer(window));
  if (handler == nullptr) { return; }

  if (action == GLFW_PRESS || action == GLFW_RELEASE) {
    std::array<int, MovementClock::kMovementCount> const keys{ handler->bindings_.forward,
      handler->bindings_.backward,
      handler->bindings_.left,
      handler->bindings_.right,
      handler->bindings_.up,
      handler->bindings_.down,
      handler->bindings_.roll_left,
      handler->bindings_.roll_right };
    for (size_t index = 0; index < keys.size(); ++index) {
      if (key == keys.at(index)) {
        handler->movement_clock_.set_pressed(index, action == GLFW_PRESS, glfwGetTime());
      }
    }
  }
  if (handler->previous_key_callback_ != nullptr) { handler->previous_key_callback_(window, key, scancode, action, mods); }
}

auto InputHandler::focus_callback(GLFWwindow *window, int focused) -> void
{
  auto *const handler = static_cast<InputHandler *>(glfwGetWindowUserPointer(window));
  if (handler == nullptr) { return; }
  handler->movement_clock_.advance(glfwGetTime());
  handler->first_mouse_ = true;
  if (focused == GLFW_FALSE) { handler->movement_clock_.release_all(glfwGetTime()); }
  if (handler->previous_focus_callback_ != nullptr) { handler->previous_focus_callback_(window, focused); }
}

auto InputHandler::movement_active() const noexcept -> bool
{
  return movement_clock_.active();
}

InputHandler::InputHandler(Window &window, KeyBindings bindings)
  : window_(static_cast<GLFWwindow *>(window.native_handle())), bindings_(bindings)
{
  if (window_ == nullptr) { return; }

  previous_user_pointer_ = glfwGetWindowUserPointer(window_);
  glfwSetWindowUserPointer(window_, this);
  glfwSetScrollCallback(window_, scroll_callback);
  previous_key_callback_ = glfwSetKeyCallback(window_, key_callback);
  previous_focus_callback_ = glfwSetWindowFocusCallback(window_, focus_callback);
  glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  movement_clock_.reset(glfwGetTime());
  glfwGetCursorPos(window_, &last_cursor_x_, &last_cursor_y_);
}

InputHandler::~InputHandler()
{
  if (window_ == nullptr) { return; }

  glfwSetScrollCallback(window_, nullptr);
  glfwSetKeyCallback(window_, previous_key_callback_);
  glfwSetWindowFocusCallback(window_, previous_focus_callback_);
  glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  glfwSetWindowUserPointer(window_, previous_user_pointer_);
}

InputHandler::InputHandler(InputHandler &&other) noexcept
  : window_(std::exchange(other.window_, nullptr)), bindings_(other.bindings_), movement_clock_(other.movement_clock_),
    last_cursor_x_(other.last_cursor_x_), last_cursor_y_(other.last_cursor_y_), scroll_y_(other.scroll_y_),
    previous_key_callback_(other.previous_key_callback_), previous_focus_callback_(other.previous_focus_callback_),
    first_mouse_(other.first_mouse_), screenshot_was_pressed_(other.screenshot_was_pressed_),
    screenshot_requested_(other.screenshot_requested_), emulate_was_pressed_(other.emulate_was_pressed_),
    emulate_toggle_requested_(other.emulate_toggle_requested_), left_was_pressed_(other.left_was_pressed_),
    right_was_pressed_(other.right_was_pressed_), up_was_pressed_(other.up_was_pressed_),
    down_was_pressed_(other.down_was_pressed_), pending_arrow_(other.pending_arrow_),
    previous_user_pointer_(other.previous_user_pointer_)
{
  if (window_ != nullptr) { glfwSetWindowUserPointer(window_, this); }
}

auto InputHandler::operator=(InputHandler &&other) noexcept -> InputHandler &
{
  if (this == &other) { return *this; }

  if (window_ != nullptr) {
    glfwSetScrollCallback(window_, nullptr);
    glfwSetKeyCallback(window_, previous_key_callback_);
    glfwSetWindowFocusCallback(window_, previous_focus_callback_);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowUserPointer(window_, previous_user_pointer_);
  }

  window_ = std::exchange(other.window_, nullptr);
  bindings_ = other.bindings_;
  movement_clock_ = other.movement_clock_;
  last_cursor_x_ = other.last_cursor_x_;
  last_cursor_y_ = other.last_cursor_y_;
  scroll_y_ = other.scroll_y_;
  previous_key_callback_ = other.previous_key_callback_;
  previous_focus_callback_ = other.previous_focus_callback_;
  first_mouse_ = other.first_mouse_;
  screenshot_was_pressed_ = other.screenshot_was_pressed_;
  screenshot_requested_ = other.screenshot_requested_;
  emulate_was_pressed_ = other.emulate_was_pressed_;
  emulate_toggle_requested_ = other.emulate_toggle_requested_;
  left_was_pressed_ = other.left_was_pressed_;
  right_was_pressed_ = other.right_was_pressed_;
  up_was_pressed_ = other.up_was_pressed_;
  down_was_pressed_ = other.down_was_pressed_;
  pending_arrow_ = other.pending_arrow_;
  previous_user_pointer_ = other.previous_user_pointer_;

  if (window_ != nullptr) { glfwSetWindowUserPointer(window_, this); }

  return *this;
}

auto InputHandler::consume_screenshot_request() noexcept -> bool
{
  auto const requested = screenshot_requested_;
  screenshot_requested_ = false;
  return requested;
}

auto InputHandler::consume_emulate_toggle() noexcept -> bool
{
  auto const requested = emulate_toggle_requested_;
  emulate_toggle_requested_ = false;
  return requested;
}

auto InputHandler::consume_arrow() noexcept -> ArrowDir
{
  auto const dir = pending_arrow_;
  pending_arrow_ = ArrowDir::kNone;
  return dir;
}

}// namespace vkgsplat
