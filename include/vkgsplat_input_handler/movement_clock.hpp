#ifndef VKGSPLAT_INPUT_HANDLER_MOVEMENT_CLOCK_HPP
#define VKGSPLAT_INPUT_HANDLER_MOVEMENT_CLOCK_HPP

#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace vkgsplat {

class MovementClock
{
public:
  // Transition times are callback dispatch times. Delayed GLFW events cannot
  // recover the physical time of a press or release during a blocked event loop.
  static constexpr size_t kForwardIndex = 0U;
  static constexpr size_t kBackwardIndex = 1U;
  static constexpr size_t kLeftIndex = 2U;
  static constexpr size_t kRightIndex = 3U;
  static constexpr size_t kUpIndex = 4U;
  static constexpr size_t kDownIndex = 5U;
  static constexpr size_t kRollLeftIndex = 6U;
  static constexpr size_t kRollRightIndex = 7U;
  static constexpr size_t kMovementCount = kRollRightIndex + 1U;

  auto reset(f64 now) -> void
  {
    last_time_ = now;
    pressed_.fill(false);
    seconds_.fill(0.0);
  }

  auto advance(f64 now) -> void
  {
    auto const elapsed = now - last_time_;
    if (elapsed > 0.0) {
      for (size_t index = 0; index < kMovementCount; ++index) {
        if (pressed_.at(index)) { seconds_.at(index) += elapsed; }
      }
    }
    last_time_ = now;
  }

  auto set_pressed(size_t index, bool pressed, f64 now) -> void
  {
    advance(now);
    pressed_.at(index) = pressed;
  }

  auto release_all(f64 now) -> void
  {
    advance(now);
    pressed_.fill(false);
  }

  [[nodiscard]] auto consume_duration(size_t index) -> f64
  {
    auto const duration = seconds_.at(index);
    seconds_.at(index) = 0.0;
    return duration;
  }

  [[nodiscard]] auto active() const noexcept -> bool
  { return std::ranges::any_of(pressed_, [](bool pressed) -> bool { return pressed; }); }

private:
  f64 last_time_{};
  std::array<bool, kMovementCount> pressed_{};
  std::array<f64, kMovementCount> seconds_{};
};

}// namespace vkgsplat

#endif// VKGSPLAT_INPUT_HANDLER_MOVEMENT_CLOCK_HPP
