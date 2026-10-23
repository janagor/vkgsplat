// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include "../src/vkgsplat_backend/vulkan/video/h264_gop.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {
constexpr std::uint32_t kIdrInterval = 30U;
constexpr std::uint64_t kFrameCount = 95U;
}// namespace

// Exercises the complete GOP policy across IDR and P picture branches.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("H.264 GOP selects deterministic IDR positions and resets numbering", "[video]")
{
  auto scheduler = vkgsplat::vulkan::video::H264Gop::Create(kIdrInterval, true);
  REQUIRE(scheduler);
  for (std::uint64_t index = 0U; index < kFrameCount; ++index) {
    auto const picture = scheduler->picture(index);
    auto const idr = index % kIdrInterval == 0U;
    CHECK((picture.type == vkgsplat::vulkan::video::PictureType::kIdr) == idr);
    CHECK(picture.frame_index == index);
    CHECK(picture.remaining_frames == kIdrInterval - 1U - (index % kIdrInterval));
    if (idr) {
      CHECK(picture.frame_num == 0U);
      CHECK(picture.poc_lsb == 0U);
    }
  }
}

TEST_CASE("H.264 GOP can request all-intra pictures", "[video]")
{
  CHECK_FALSE(vkgsplat::vulkan::video::H264Gop::Create(0U, true));
  auto scheduler = vkgsplat::vulkan::video::H264Gop::Create(kIdrInterval, false);
  REQUIRE(scheduler);
  CHECK(scheduler->picture(17U).type == vkgsplat::vulkan::video::PictureType::kIdr);
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
