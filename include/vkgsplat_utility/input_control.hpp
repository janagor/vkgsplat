#ifndef VKGSPLAT_UTILITY_INPUT_CONTROL_HPP
#define VKGSPLAT_UTILITY_INPUT_CONTROL_HPP

#include <concepts>

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

enum class ViewMovement : u8 {
  kForward,
  kBackward,
  kLeft,
  kRight,
  kUp,
  kDown,
  kRollLeft,
  kRollRight,
};

template<class T>
concept KeyboardControllable =
  requires(T &target, ViewMovement direction, f64 delta_time) { target.process_keyboard(direction, delta_time); };

template<class T>
concept MouseLookControllable =
  requires(T &target, f64 x_offset, f64 y_offset) { target.process_mouse_movement(x_offset, y_offset); };

template<class T>
concept ScrollZoomable = requires(T &target, f64 y_offset) { target.process_mouse_scroll(y_offset); };

template<class T>
concept Closeable = requires(T &target, T const &const_target) {
  target.request_close();
  { const_target.close_requested() } -> std::convertible_to<bool>;
};

class CloseState
{
public:
  void request_close() noexcept { close_requested_ = true; }
  [[nodiscard]] auto close_requested() const noexcept -> bool { return close_requested_; }

private:
  bool close_requested_ = false;
};

static_assert(Closeable<CloseState>);

}// namespace vkgsplat

#endif// VKGSPLAT_UTILITY_INPUT_CONTROL_HPP
