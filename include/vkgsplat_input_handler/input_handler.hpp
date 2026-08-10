#ifndef VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_HPP
#define VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_HPP

#include <vkgsplat_utility/input_control.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_window/window.hpp>

struct GLFWwindow;

namespace vkgsplat {

class InputHandler
{
public:
  explicit InputHandler(Window &window);
  ~InputHandler();

  InputHandler(InputHandler const & /*other*/) = delete;
  auto operator=(InputHandler const & /*other*/) -> InputHandler & = delete;
  InputHandler(InputHandler &&other) noexcept;
  auto operator=(InputHandler &&other) noexcept -> InputHandler &;

  template<class View, class CloseTarget>
    requires KeyboardControllable<View> && MouseLookControllable<View> && ScrollZoomable<View> && Closeable<CloseTarget>
  // Returns true when the view was modified (needs a redraw).
  [[nodiscard]] auto update(View &view, CloseTarget &close) -> bool;

  // Edge-triggered: true once per F2 press until consumed.
  [[nodiscard]] auto consume_screenshot_request() noexcept -> bool;
  [[nodiscard]] auto screenshot_requested() const noexcept -> bool { return screenshot_requested_; }

private:
  static void scroll_callback(GLFWwindow *window, f64 /*x_offset*/, f64 y_offset);

  GLFWwindow *window_ = nullptr;
  f64 last_time_ = 0.0;
  f64 last_cursor_x_ = 0.0;
  f64 last_cursor_y_ = 0.0;
  f64 scroll_y_ = 0.0;
  bool first_mouse_ = true;
  bool f2_was_pressed_ = false;
  bool screenshot_requested_ = false;
  void *previous_user_pointer_ = nullptr;
};

}// namespace vkgsplat

#include <vkgsplat_input_handler/input_handler.ipp>// IWYU pragma: export

#endif// VKGSPLAT_INPUT_HANDLER_INPUT_HANDLER_HPP
