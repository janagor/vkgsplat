// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include <vkgsplat_animation/camera_path.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {
constexpr double kPathDurationSeconds = 2.0;
constexpr auto kValidPath = R"({"version":1,"keyframes":[
  {"time":0,"position":[0,1,10],"target":[0,0,0],"fov_degrees":45},
  {"time":2,"position":[2,1,10],"orientation":[0,0,0,1],"fov_degrees":40}
]})";
}// namespace

TEST_CASE("camera path JSON accepts look-at and quaternion keyframes", "[animation]")
{
  auto path = vkgsplat::animation::ParseCameraPathJson(kValidPath);
  REQUIRE(path);
  REQUIRE(path->keyframes().size() == 2U);
  REQUIRE(path->duration_seconds() == kPathDurationSeconds);
}

TEST_CASE("camera path JSON identifies invalid fields", "[animation]")
{
  REQUIRE_FALSE(vkgsplat::animation::ParseCameraPathJson(R"({"version":2,"keyframes":[]})"));
  REQUIRE_FALSE(vkgsplat::animation::ParseCameraPathJson(R"({"version":1,"keyframes":[]})"));
  auto const *const invalid = R"({"version":1,"keyframes":[
    {"time":0,"position":[0,0,0],"target":[0,0,0],"fov_degrees":45},
    {"time":1,"position":[1,0,0],"target":[0,0,0],"fov_degrees":45}
  ]})";
  auto result = vkgsplat::animation::ParseCameraPathJson(invalid);
  REQUIRE_FALSE(result);
  REQUIRE(result.error().message().contains("keyframes[0].target"));

  auto const *const zero_quaternion = R"({"version":1,"keyframes":[
    {"time":0,"position":[0,0,1],"orientation":[0,0,0,0],"fov_degrees":45},
    {"time":1,"position":[1,0,1],"orientation":[0,0,0,1],"fov_degrees":45}
  ]})";
  auto quaternion_result = vkgsplat::animation::ParseCameraPathJson(zero_quaternion);
  REQUIRE_FALSE(quaternion_result);
  REQUIRE(quaternion_result.error().message().contains("keyframes[0].orientation"));

  auto const *const parallel_up = R"({"version":1,"keyframes":[
    {"time":0,"position":[0,0,1],"target":[0,0,0],"up":[0,0,1],"fov_degrees":45},
    {"time":1,"position":[1,0,1],"target":[0,0,0],"fov_degrees":45}
  ]})";
  auto up_result = vkgsplat::animation::ParseCameraPathJson(parallel_up);
  REQUIRE_FALSE(up_result);
  REQUIRE(up_result.error().message().contains("keyframes[0].up"));
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
