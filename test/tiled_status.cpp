#include "src/vkgsplat_backend/vulkan/gs/tiled_compute/operations.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr vkgsplat::u32 kCapacityOverflow = 1U << 0U;
constexpr vkgsplat::u32 kCountEmitMismatch = 1U << 1U;
constexpr vkgsplat::u32 kCountOverflow = 1U << 2U;
constexpr vkgsplat::u32 kDispatchOverflow = 1U << 3U;
constexpr vkgsplat::u32 kInvalidTileId = 1U << 4U;
constexpr vkgsplat::u32 kInvalidRasterMetadata = 1U << 5U;
constexpr vkgsplat::u32 kUnknownStatus = 1U << 6U;

struct StatusCase
{
  vkgsplat::u32 bits{};
  std::string_view reason;
};

constexpr std::array kStatusCases{
  StatusCase{ .bits = kCapacityOverflow, .reason = "intersection capacity exceeded" },
  StatusCase{ .bits = kCountEmitMismatch, .reason = "tile count and emission disagree" },
  StatusCase{ .bits = kCountOverflow, .reason = "intersection count exceeded 32 bits" },
  StatusCase{ .bits = kDispatchOverflow, .reason = "radix dispatch exceeds device limits" },
  StatusCase{ .bits = kInvalidTileId, .reason = "invalid tile ID" },
  StatusCase{ .bits = kInvalidRasterMetadata, .reason = "invalid rasterizer metadata" },
  StatusCase{ .bits = kUnknownStatus, .reason = "unknown tiled GPU error" },
};

}// namespace

TEST_CASE("tiled GPU status bits produce deterministic errors", "[tiled][status]")
{
  REQUIRE(vkgsplat::gs::tiled::DecodeFrameStatus(0U).has_value());
  for (auto const &item : kStatusCases) {
    auto const decoded = vkgsplat::gs::tiled::DecodeFrameStatus(item.bits);
    REQUIRE(!decoded.has_value());
    CHECK(decoded.error().code() == std::errc::result_out_of_range);
    CHECK(decoded.error().message().contains(item.reason));
  }
}
