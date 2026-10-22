// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include "../benchmark/gpu_statistics.hpp"
#include "../src/vkgsplat_backend/vulkan/gpu_timestamp_math.hpp"
#include "../src/vkgsplat_backend/vulkan/gpu_timing_samples.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <numbers>

#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat_utility/types.hpp>

namespace {

constexpr vkgsplat::u32 kFullTimestampBits = 64U;
constexpr vkgsplat::u32 kWrappingTimestampBits = 8U;
constexpr vkgsplat::u32 kUnsupportedTimestampBits = 0U;
constexpr vkgsplat::u64 kIntervalBeginTicks = 100U;
constexpr vkgsplat::u64 kIntervalEndTicks = 200U;
constexpr vkgsplat::u64 kWrapBeginTicks = 250U;
constexpr vkgsplat::u64 kWrapEndTicks = 5U;
constexpr vkgsplat::u64 kFirstTick = 1U;
constexpr vkgsplat::u64 kSecondTick = 2U;
constexpr vkgsplat::u64 kUnavailable = 0U;
constexpr vkgsplat::u64 kAvailable = 1U;
constexpr double kOneNanosecondPeriod = 1.0;
constexpr double kTwoNanosecondPeriod = 2.0;
constexpr double kExpectedIntervalMs = 0.0002;
constexpr double kExpectedWrapMs = 0.000011;
constexpr double kExpectedSingleTickMs = 0.000001;
constexpr double kAbsentDurationMs = -1.0;

constexpr vkgsplat::u64 kFirstFrameId = 0U;
constexpr vkgsplat::u64 kSecondFrameId = 1U;
constexpr vkgsplat::u64 kThirdFrameId = 2U;
constexpr vkgsplat::u64 kFourthFrameId = 3U;
constexpr size_t kNoSamples = 0U;
constexpr size_t kFirstSampleIndex = 0U;
constexpr size_t kSecondSampleIndex = 1U;
constexpr size_t kThirdSampleIndex = 2U;
constexpr size_t kOneSample = 1U;
constexpr size_t kTwoSamples = 2U;
constexpr size_t kThreeSamples = 3U;
constexpr size_t kFiveSamples = 5U;
constexpr double kZeroMs = 0.0;
constexpr double kOneMs = 1.0;
constexpr double kOneAndHalfMs = 1.5;
constexpr double kTwoMs = 2.0;
constexpr double kTwoAndHalfMs = 2.5;
constexpr double kThreeMs = 3.0;
constexpr double kFourMs = 4.0;
constexpr double kFiveMs = 5.0;
constexpr double kSixMs = 6.0;
constexpr double kEightMs = 8.0;
constexpr double kNegativeMs = -1.0;
constexpr double kExpectedPopulationDeviation = std::numbers::sqrt2;
constexpr size_t kInitialReservation = 2U;
constexpr vkgsplat::u64 kFirstBatchFrames = 5U;
constexpr vkgsplat::u64 kSecondBatchFrames = 7U;
constexpr vkgsplat::u64 kSingleFrame = 1U;

}// namespace

TEST_CASE("GPU timestamp conversion handles counter wraparound")
{
  using vkgsplat::TimestampDeltaMs;
  auto const interval =
    TimestampDeltaMs(kIntervalBeginTicks, kIntervalEndTicks, kFullTimestampBits, kTwoNanosecondPeriod);
  REQUIRE(interval.has_value());
  REQUIRE(interval.value_or(kAbsentDurationMs) == Catch::Approx(kExpectedIntervalMs));

  auto const wrapped = TimestampDeltaMs(kWrapBeginTicks, kWrapEndTicks, kWrappingTimestampBits, kOneNanosecondPeriod);
  REQUIRE(wrapped.has_value());
  REQUIRE(wrapped.value_or(kAbsentDurationMs) == Catch::Approx(kExpectedWrapMs));
  REQUIRE_FALSE(TimestampDeltaMs(kUnavailable, kFirstTick, kUnsupportedTimestampBits, kOneNanosecondPeriod));

  auto const unavailable = vkgsplat::AvailableTimestampPairMs(
    { kFirstTick, kUnavailable, kSecondTick, kAvailable }, kFullTimestampBits, kOneNanosecondPeriod);
  REQUIRE_FALSE(unavailable);
  auto const available = vkgsplat::AvailableTimestampPairMs(
    { kFirstTick, kAvailable, kSecondTick, kAvailable }, kFullTimestampBits, kOneNanosecondPeriod);
  REQUIRE(available.has_value());
  REQUIRE(available.value_or(kAbsentDurationMs) == Catch::Approx(kExpectedSingleTickMs));
}

TEST_CASE("GPU statistics use nearest rank and population deviation")
{
  auto const summary = vkgsplat::benchmark::SummarizeGpuValues({ kOneMs, kTwoMs, kThreeMs, kFourMs, kFiveMs });
  REQUIRE(summary.count == kFiveSamples);
  REQUIRE(summary.mean == Catch::Approx(kThreeMs));
  REQUIRE(summary.median == Catch::Approx(kThreeMs));
  REQUIRE(summary.p95 == Catch::Approx(kFiveMs));
  REQUIRE(summary.minimum == Catch::Approx(kOneMs));
  REQUIRE(summary.maximum == Catch::Approx(kFiveMs));
  REQUIRE(summary.standard_deviation == Catch::Approx(kExpectedPopulationDeviation));
}

TEST_CASE("GPU statistics preserve absent passes")
{
  vkgsplat::benchmark::GpuStatistics statistics;
  vkgsplat::GpuFrameTimings sample{ .frame_id = kSecondFrameId, .frame_ms = kTwoMs };
  sample.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kProjection)) = kZeroMs;
  statistics.add(sample);
  REQUIRE(statistics.frame_count() == kOneSample);
  REQUIRE(statistics.pass(vkgsplat::GpuPass::kProjection).count == kOneSample);
  REQUIRE(statistics.pass(vkgsplat::GpuPass::kMultiviewProjection).count == kNoSamples);
  REQUIRE(statistics.pass(vkgsplat::GpuPass::kMultiviewProjection).mean == kZeroMs);
}

TEST_CASE("GPU statistics accumulate multiview pass values")
{
  vkgsplat::benchmark::GpuStatistics statistics;
  vkgsplat::GpuFrameTimings first{ .frame_id = kSecondFrameId, .frame_ms = kSixMs };
  first.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kMultiviewProjection)) = kTwoMs;
  vkgsplat::GpuFrameTimings second{ .frame_id = kThirdFrameId, .frame_ms = kEightMs };
  second.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kMultiviewProjection)) = kFourMs;
  statistics.add(first);
  statistics.add(second);
  auto const summary = statistics.pass(vkgsplat::GpuPass::kMultiviewProjection);
  REQUIRE(summary.count == kTwoSamples);
  REQUIRE(summary.mean == Catch::Approx(kThreeMs));
  REQUIRE(summary.median == Catch::Approx(kThreeMs));
}

TEST_CASE("GPU timing sample buffer transfers ordered frames exactly once")
{
  vkgsplat::GpuTimingSamples samples;
  samples.reserve(kThreeSamples);
  samples.publish(vkgsplat::GpuFrameTimings{ .frame_id = kThirdFrameId, .frame_ms = kTwoMs });
  samples.publish(vkgsplat::GpuFrameTimings{ .frame_id = kFirstFrameId, .frame_ms = kOneMs });
  samples.publish(vkgsplat::GpuFrameTimings{ .frame_id = kSecondFrameId, .frame_ms = kOneAndHalfMs });

  auto first = samples.take();
  REQUIRE(first.size() == kThreeSamples);
  REQUIRE(first.at(kFirstSampleIndex).frame_id == kFirstFrameId);
  REQUIRE(first.at(kSecondSampleIndex).frame_id == kSecondFrameId);
  REQUIRE(first.at(kThirdSampleIndex).frame_id == kThirdFrameId);
  REQUIRE(samples.take().empty());

  samples.publish(vkgsplat::GpuFrameTimings{ .frame_id = kFourthFrameId, .frame_ms = kTwoAndHalfMs });
  auto second = samples.take();
  REQUIRE(second.size() == kOneSample);
  REQUIRE(second.front().frame_id == kFourthFrameId);
  REQUIRE(samples.take().empty());
}

TEST_CASE("GPU timing sample buffer reset removes pending frames")
{
  vkgsplat::GpuTimingSamples samples;
  samples.publish(vkgsplat::GpuFrameTimings{ .frame_id = kFirstFrameId });
  samples.reset();
  REQUIRE(samples.take().empty());
  samples.publish(vkgsplat::GpuFrameTimings{ .frame_id = kSecondFrameId });
  REQUIRE(samples.take().front().frame_id == kSecondFrameId);
}

TEST_CASE("mono GPU sample validation requires all finite passes")
{
  vkgsplat::GpuFrameTimings sample{ .frame_id = kFirstFrameId, .frame_ms = kFourMs };
  REQUIRE_FALSE(vkgsplat::benchmark::IsCompleteMonoGpuSample(sample));
  for (size_t index = kFirstSampleIndex; index < static_cast<size_t>(vkgsplat::GpuPass::kMultiviewProjection);
    ++index) {
    sample.pass_ms.at(index) = kOneMs;
  }
  REQUIRE(vkgsplat::benchmark::IsCompleteMonoGpuSample(sample));
  sample.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kRadixSort)) = kNegativeMs;
  REQUIRE_FALSE(vkgsplat::benchmark::IsCompleteMonoGpuSample(sample));
  sample.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kRadixSort)) = std::numeric_limits<double>::quiet_NaN();
  REQUIRE_FALSE(vkgsplat::benchmark::IsCompleteMonoGpuSample(sample));
  sample.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kRadixSort)) = kZeroMs;
  REQUIRE(vkgsplat::benchmark::IsCompleteMonoGpuSample(sample));
  sample.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kMultiviewProjection)) = kZeroMs;
  REQUIRE_FALSE(vkgsplat::benchmark::IsCompleteMonoGpuSample(sample));
}

TEST_CASE("mono GPU statistics require every pass for every frame")
{
  vkgsplat::benchmark::GpuStatistics statistics;
  vkgsplat::GpuFrameTimings sample{ .frame_id = kFirstFrameId, .frame_ms = kFourMs };
  for (size_t index = kFirstSampleIndex; index < static_cast<size_t>(vkgsplat::GpuPass::kMultiviewProjection);
    ++index) {
    sample.pass_ms.at(index) = kOneMs;
  }
  statistics.add(sample);
  REQUIRE(vkgsplat::benchmark::HasCompleteMonoGpuStatistics(statistics));
  sample.frame_id = kSecondFrameId;
  sample.pass_ms.at(static_cast<size_t>(vkgsplat::GpuPass::kProjection)).reset();
  statistics.add(sample);
  REQUIRE_FALSE(vkgsplat::benchmark::HasCompleteMonoGpuStatistics(statistics));
}

TEST_CASE("GPU timing sample buffer accepts larger batches after reserve and drain cycles")
{
  vkgsplat::GpuTimingSamples samples;
  samples.reserve(kInitialReservation);
  for (vkgsplat::u64 frame = kFirstFrameId; frame < kFirstBatchFrames; ++frame) {
    samples.publish(vkgsplat::GpuFrameTimings{ .frame_id = frame });
  }
  auto first = samples.take();
  REQUIRE(first.size() == kFirstBatchFrames);
  REQUIRE(first.front().frame_id == kFirstFrameId);
  REQUIRE(first.back().frame_id == kFirstBatchFrames - kSingleFrame);

  samples.reserve(kInitialReservation);
  for (vkgsplat::u64 frame = kFirstBatchFrames; frame < kFirstBatchFrames + kSecondBatchFrames; ++frame) {
    samples.publish(vkgsplat::GpuFrameTimings{ .frame_id = frame });
  }
  auto second = samples.take();
  REQUIRE(second.size() == kSecondBatchFrames);
  REQUIRE(second.front().frame_id == kFirstBatchFrames);
  REQUIRE(second.back().frame_id == kFirstBatchFrames + kSecondBatchFrames - kSingleFrame);
  REQUIRE(samples.take().empty());
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
