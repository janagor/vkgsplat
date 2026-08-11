#include <vkgsplat_input_handler/input_handler.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_window/window.hpp>

#include <GLFW/glfw3.h>

#include <utility>

namespace vkgsplat {

void InputHandler::scroll_callback(GLFWwindow *window, f64 /*x_offset*/, f64 y_offset)
{
  auto *const handler = static_cast<InputHandler *>(glfwGetWindowUserPointer(window));
  if (handler == nullptr) { return; }
  handler->scroll_y_ += y_offset;
}

InputHandler::InputHandler(Window &window) : window_(static_cast<GLFWwindow *>(window.native_handle()))
{
  if (window_ == nullptr) { return; }

  previous_user_pointer_ = glfwGetWindowUserPointer(window_);
  glfwSetWindowUserPointer(window_, this);
  glfwSetScrollCallback(window_, scroll_callback);
  glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  last_time_ = glfwGetTime();
  glfwGetCursorPos(window_, &last_cursor_x_, &last_cursor_y_);
}

InputHandler::~InputHandler()
{
  if (window_ == nullptr) { return; }

  glfwSetScrollCallback(window_, nullptr);
  glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  glfwSetWindowUserPointer(window_, previous_user_pointer_);
}

InputHandler::InputHandler(InputHandler &&other) noexcept
  : window_(std::exchange(other.window_, nullptr)), last_time_(other.last_time_), last_cursor_x_(other.last_cursor_x_),
    last_cursor_y_(other.last_cursor_y_), scroll_y_(other.scroll_y_), first_mouse_(other.first_mouse_),
    f2_was_pressed_(other.f2_was_pressed_), screenshot_requested_(other.screenshot_requested_),
    previous_user_pointer_(other.previous_user_pointer_)
{
  if (window_ != nullptr) { glfwSetWindowUserPointer(window_, this); }
}

auto InputHandler::operator=(InputHandler &&other) noexcept -> InputHandler &
{
  if (this == &other) { return *this; }

  if (window_ != nullptr) {
    glfwSetScrollCallback(window_, nullptr);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowUserPointer(window_, previous_user_pointer_);
  }

  window_ = std::exchange(other.window_, nullptr);
  last_time_ = other.last_time_;
  last_cursor_x_ = other.last_cursor_x_;
  last_cursor_y_ = other.last_cursor_y_;
  scroll_y_ = other.scroll_y_;
  first_mouse_ = other.first_mouse_;
  f2_was_pressed_ = other.f2_was_pressed_;
  screenshot_requested_ = other.screenshot_requested_;
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
