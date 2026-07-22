#pragma once

#include <vkgsplat_utility/input_control.hpp>
#include <vkgsplat_utility/types.hpp>

struct GLFWwindow;

namespace vkgsplat {

class InputHandler
{
public:
  explicit InputHandler(GLFWwindow *window);
  ~InputHandler();

  InputHandler(InputHandler const &) = delete;
  auto operator=(InputHandler const &) -> InputHandler & = delete;
  InputHandler(InputHandler &&) noexcept;
  auto operator=(InputHandler &&) noexcept -> InputHandler &;

  template<class View, class CloseTarget>
    requires KeyboardControllable<View> && MouseLookControllable<View> && ScrollZoomable<View> && Closeable<CloseTarget>
  void update(View &view, CloseTarget &close);

private:
  static void scroll_callback(GLFWwindow *window, f64 /*x_offset*/, f64 y_offset);

  GLFWwindow *window_ = nullptr;
  f64 last_time_ = 0.0;
  f64 last_cursor_x_ = 0.0;
  f64 last_cursor_y_ = 0.0;
  f64 scroll_y_ = 0.0;
  bool first_mouse_ = true;
  void *previous_user_pointer_ = nullptr;
};

}// namespace vkgsplat

#include <vkgsplat_input_handler/input_handler.ipp>// IWYU pragma: export
