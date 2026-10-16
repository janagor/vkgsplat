#include <vkgsplat/camera.hpp>
#include <vkgsplat_input_handler/movement_clock.hpp>
#include <vkgsplat_utility/input_control.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <initializer_list>

#include <glm/geometric.hpp>

namespace {

constexpr double kStartTimeSeconds = 0.0;
constexpr double kFirstSlowUpdateSeconds = 0.1;
constexpr double kSecondSlowUpdateSeconds = 0.2;
constexpr double kThirdSlowUpdateSeconds = 0.5;
constexpr double kOneSecond = 1.0;
constexpr double kCameraSpeed = 5.5;
constexpr double kPositionTolerance = 1e-12;
constexpr double kIdleSeconds = 5.0;
constexpr double kShortPressSeconds = 0.01;
constexpr double kPressTimeSeconds = 0.02;
constexpr double kReleaseTimeSeconds = 0.06;
constexpr double kTapDurationSeconds = kReleaseTimeSeconds - kPressTimeSeconds;
constexpr double kDispatchTimeSeconds = 0.1;

}// namespace

TEST_CASE("movement distance survives slow updates", "[input]")
{
  vkgsplat::MovementClock clock;
  vkgsplat::Camera camera{ vkgsplat::CameraConfig{ .movement_speed = kCameraSpeed } };
  auto const initial_position = camera.position();
  auto const initial_front = camera.front();
  clock.reset(kStartTimeSeconds);
  clock.set_pressed(vkgsplat::MovementClock::kForwardIndex, true, kStartTimeSeconds);

  double duration = kStartTimeSeconds;
  for (double const now : { kFirstSlowUpdateSeconds, kSecondSlowUpdateSeconds, kThirdSlowUpdateSeconds, kOneSecond }) {
    clock.advance(now);
    auto const step = clock.consume_duration(vkgsplat::MovementClock::kForwardIndex);
    duration += step;
    camera.process_keyboard(vkgsplat::ViewMovement::kForward, step);
  }
  REQUIRE(duration == Catch::Approx(kOneSecond));
  auto const expected_position = initial_position + initial_front * kCameraSpeed;
  REQUIRE(glm::distance(camera.position(), expected_position) < kPositionTolerance);
}

TEST_CASE("idle time is excluded from a new press", "[input]")
{
  vkgsplat::MovementClock clock;
  clock.reset(kStartTimeSeconds);
  clock.set_pressed(vkgsplat::MovementClock::kForwardIndex, true, kIdleSeconds);
  clock.advance(kIdleSeconds + kShortPressSeconds);
  REQUIRE(clock.consume_duration(vkgsplat::MovementClock::kForwardIndex) == Catch::Approx(kShortPressSeconds));
}

TEST_CASE("press and release between updates are retained", "[input]")
{
  vkgsplat::MovementClock clock;
  clock.reset(kStartTimeSeconds);
  clock.set_pressed(vkgsplat::MovementClock::kForwardIndex, true, kPressTimeSeconds);
  clock.set_pressed(vkgsplat::MovementClock::kForwardIndex, false, kReleaseTimeSeconds);
  clock.advance(kDispatchTimeSeconds);
  REQUIRE(clock.consume_duration(vkgsplat::MovementClock::kForwardIndex) == Catch::Approx(kTapDurationSeconds));
  REQUIRE_FALSE(clock.active());
}

TEST_CASE("delayed callback timestamps cannot recover physical hold time", "[input]")
{
  vkgsplat::MovementClock clock;
  clock.reset(kStartTimeSeconds);
  clock.set_pressed(vkgsplat::MovementClock::kForwardIndex, true, kDispatchTimeSeconds);
  clock.set_pressed(vkgsplat::MovementClock::kForwardIndex, false, kDispatchTimeSeconds);
  REQUIRE(clock.consume_duration(vkgsplat::MovementClock::kForwardIndex) == Catch::Approx(kStartTimeSeconds));
}
