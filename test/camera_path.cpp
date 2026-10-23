#include <vkgsplat/camera.hpp>
#include <vkgsplat_animation/camera_path.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr vkgsplat::f64 kFirstFov = 45.0;
constexpr vkgsplat::f64 kSecondFov = 55.0;
constexpr vkgsplat::f64 kFirstTime = 0.0;
constexpr vkgsplat::f64 kSecondTime = 2.0;
constexpr vkgsplat::f64 kThirdTime = 4.0;
constexpr vkgsplat::f64 kQuarterTurnDegrees = 90.0;
constexpr vkgsplat::f64 kRotationTolerance = 1e-12;
constexpr std::size_t kMatrixDimension = 4U;

[[nodiscard]] auto Key(vkgsplat::f64 time, vkgsplat::f64 x, vkgsplat::f64 fov = kFirstFov)
  -> vkgsplat::animation::CameraKeyframe
{
  return { .time_seconds = time,
    .pose = { .position = { x, 0.0, 0.0 }, .orientation = glm::dquat{ 1.0, 0.0, 0.0, 0.0 },
      .fov_degrees = fov } };
}

}// namespace

TEST_CASE("camera path validates and samples exact keyframes", "[animation]")
{
  auto path = vkgsplat::animation::CameraPath::create(
    { Key(kFirstTime, 0.0), Key(kSecondTime, 2.0), Key(kThirdTime, 4.0) });
  REQUIRE(path);
  REQUIRE(path->duration_seconds() == kThirdTime);
  REQUIRE(path->sample(kSecondTime)->position.x == 2.0);
  REQUIRE(path->sample(-1.0)->position.x == 0.0);
  REQUIRE(path->sample(kThirdTime + 1.0)->position.x == 4.0);
  REQUIRE_FALSE(path->sample(std::numeric_limits<vkgsplat::f64>::infinity()));

  REQUIRE_FALSE(vkgsplat::animation::CameraPath::create({ Key(kFirstTime, 0.0) }));
  REQUIRE_FALSE(vkgsplat::animation::CameraPath::create({ Key(kFirstTime, 0.0), Key(kFirstTime, 1.0) }));
  auto invalid = Key(kSecondTime, 1.0);
  invalid.pose.orientation = glm::dquat{ 0.0, 0.0, 0.0, 0.0 };
  REQUIRE_FALSE(vkgsplat::animation::CameraPath::create({ Key(kFirstTime, 0.0), invalid }));
  invalid = Key(kSecondTime, 1.0);
  invalid.pose.fov_degrees = std::numeric_limits<vkgsplat::f64>::quiet_NaN();
  REQUIRE_FALSE(vkgsplat::animation::CameraPath::create({ Key(kFirstTime, 0.0), invalid }));
}

TEST_CASE("camera path interpolates position orientation and field of view", "[animation]")
{
  auto first = Key(kFirstTime, 0.0, kFirstFov);
  auto second = Key(kSecondTime, 2.0, kSecondFov);
  second.pose.orientation = -first.pose.orientation;
  auto path = vkgsplat::animation::CameraPath::create({ first, second });
  REQUIRE(path);
  auto middle = path->sample(1.0);
  REQUIRE(middle);
  REQUIRE(middle->position.x == Catch::Approx(1.0));
  REQUIRE(middle->fov_degrees == Catch::Approx(50.0));
  REQUIRE(std::abs(glm::dot(middle->orientation, first.pose.orientation)) == Catch::Approx(1.0));

  auto cubic = vkgsplat::animation::CameraPath::create(
    { Key(kFirstTime, 0.0), Key(kSecondTime, 2.0), Key(kThirdTime, 0.0) });
  REQUIRE(cubic);
  REQUIRE(cubic->sample(kSecondTime)->position.x == 2.0);
  REQUIRE(cubic->sample(1.0)->position.x > 0.0);
  REQUIRE(cubic->sample(3.0)->position.x > 0.0);
}

TEST_CASE("camera path frame timing is rational and repeatable", "[animation]")
{
  auto path = vkgsplat::animation::CameraPath::create({ Key(kFirstTime, 0.0), Key(kSecondTime, 2.0) });
  REQUIRE(path);
  REQUIRE(vkgsplat::animation::frame_count(*path, 60U, 1U).value() == 120U);
  REQUIRE(vkgsplat::animation::frame_time_seconds(60U, 60U, 1U).value() == 1.0);
  REQUIRE(vkgsplat::animation::frame_time_seconds(30'000U, 30'000U, 1001U).value() == 1001.0);
  auto const time = vkgsplat::animation::frame_time_seconds(37U, 60U, 1U).value();
  auto const pose = path->sample(time).value();
  auto const again = path->sample(time).value();
  REQUIRE(pose.position.x == again.position.x);
  REQUIRE(pose.position.y == again.position.y);
  REQUIRE(pose.position.z == again.position.z);
  vkgsplat::Camera const first{ vkgsplat::CameraConfig{ .position = pose.position,
    .orientation = pose.orientation, .fov_degrees = pose.fov_degrees } };
  vkgsplat::Camera const repeated{ vkgsplat::CameraConfig{ .position = again.position,
    .orientation = again.orientation, .fov_degrees = again.fov_degrees } };
  for (std::size_t column = 0U; column < kMatrixDimension; ++column) {
    for (std::size_t row = 0U; row < kMatrixDimension; ++row) {
      REQUIRE(first.view_matrix()[column][row] == repeated.view_matrix()[column][row]);
    }
  }
}

TEST_CASE("camera path frame counts match sampled frame times", "[animation]")
{
  auto const count_for = [](vkgsplat::f64 duration, vkgsplat::u32 numerator, vkgsplat::u32 denominator)
    -> vkgsplat::u64 {
    auto path = vkgsplat::animation::CameraPath::create({ Key(0.0, 0.0), Key(duration, 1.0) });
    REQUIRE(path);
    return vkgsplat::animation::frame_count(*path, numerator, denominator).value();
  };
  REQUIRE(count_for(0.1, 60U, 1U) == 6U);
  REQUIRE(count_for(0.2, 30U, 1U) == 6U);
  REQUIRE(count_for(1.1, 60U, 1U) == 66U);
  REQUIRE(count_for(2.0, 60U, 1U) == 120U);
  auto const rational_boundary = vkgsplat::animation::frame_time_seconds(1U, 30'000U, 1001U).value();
  REQUIRE(count_for(rational_boundary, 30'000U, 1001U) == 1U);
  REQUIRE(count_for(std::nextafter(rational_boundary, std::numeric_limits<vkgsplat::f64>::infinity()),
            30'000U, 1001U) == 2U);
  auto const decimal_boundary = vkgsplat::animation::frame_time_seconds(6U, 60U, 1U).value();
  REQUIRE(count_for(std::nextafter(decimal_boundary, 0.0), 60U, 1U) == 6U);
  REQUIRE(count_for(std::nextafter(decimal_boundary, std::numeric_limits<vkgsplat::f64>::infinity()), 60U, 1U)
          == 7U);
}

TEST_CASE("camera path rejects unrepresentable interpolation tangents", "[animation]")
{
  auto path = vkgsplat::animation::CameraPath::create({ Key(0.0, 0.0), Key(1e-310, 1.0), Key(1.0, 2.0) });
  REQUIRE_FALSE(path);
  REQUIRE(path.error().message().find("keyframes[0].position") != std::string::npos);
}

TEST_CASE("camera path rotates the camera through a real quarter turn", "[animation]")
{
  auto first = Key(0.0, 0.0);
  auto second = Key(2.0, 0.0);
  second.pose.orientation = glm::angleAxis(glm::radians(kQuarterTurnDegrees), glm::dvec3{ 0.0, 1.0, 0.0 });
  auto path = vkgsplat::animation::CameraPath::create({ first, second });
  REQUIRE(path);
  auto midpoint = path->sample(1.0);
  REQUIRE(midpoint);
  auto const angle = glm::angle(midpoint->orientation);
  auto const expected_angle = glm::radians(kQuarterTurnDegrees) / 2.0;
  REQUIRE(angle == Catch::Approx(expected_angle).margin(kRotationTolerance));
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{ .orientation = midpoint->orientation } };
  auto const expected_component = -std::sqrt(0.5);
  REQUIRE(camera.front().x == Catch::Approx(expected_component).margin(kRotationTolerance));
  REQUIRE(camera.front().z == Catch::Approx(expected_component).margin(kRotationTolerance));
}
