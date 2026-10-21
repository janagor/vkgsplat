// NOLINTBEGIN(readability-function-cognitive-complexity)
#include "vulkan/render_thread.hpp"
#include "vulkan/vkexec_error.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <atomic>
#include <barrier>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <vector>

namespace {

inline constexpr int kProducerCount = 2;
inline constexpr int kFramesPerProducer = 100;
inline constexpr int kInitialFrameCount = 1;
inline constexpr vkgsplat::u32 kReadbackTestWidth = 1U;
inline constexpr vkgsplat::u32 kReadbackTestHeight = 1U;
inline constexpr std::uint8_t kReadbackRed = 17U;
inline constexpr std::uint8_t kReadbackGreen = 34U;
inline constexpr std::uint8_t kReadbackBlue = 51U;
inline constexpr std::uint8_t kReadbackAlpha = 255U;
inline constexpr vkgsplat::u64 kTwoFrameCount = 2U;

struct FrameBarrier
{
  std::mutex mutex;
  std::condition_variable condition;
  bool entered = false;
  bool release = false;

  auto Wait() -> void
  {
    std::unique_lock lock{ mutex };
    entered = true;
    condition.notify_all();
    condition.wait(lock, [this]() -> bool { return release; });
  }

  auto WaitUntilEntered() -> void
  {
    std::unique_lock lock{ mutex };
    condition.wait(lock, [this]() -> bool { return entered; });
  }

  auto Release() -> void
  {
    {
      std::scoped_lock const lock{ mutex };
      release = true;
    }
    condition.notify_all();
  }
};

[[nodiscard]] auto
  SubmitFrames(vkgsplat::RenderThread &worker, vkgsplat::Camera const &camera, vkgsplat::f64 aspect_ratio) -> bool
{
  for (int frame = 0; frame < kFramesPerProducer; ++frame) {
    if (!worker.SubmitFrame(camera, aspect_ratio)) { return false; }
  }
  return true;
}

}// namespace

TEST_CASE("render thread admits concurrent producers without frame replacement", "[render]")
{
  FrameBarrier barrier;
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless, [&barrier]() -> std::expected<void, vkgsplat::Error> {
    barrier.Wait();
    return {};
  });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  auto const aspect_ratio = camera.aspect_ratio();
  REQUIRE(worker.SubmitFrame(camera, aspect_ratio));
  barrier.WaitUntilEntered();

  std::barrier start{ kProducerCount + 1 };
  std::array producer_ok{ true, true };
  std::jthread first{ [&]() -> void {
    start.arrive_and_wait();
    producer_ok.at(0) = SubmitFrames(worker, camera, aspect_ratio);
  } };
  std::jthread second{ [&]() -> void {
    start.arrive_and_wait();
    producer_ok.at(1) = SubmitFrames(worker, camera, aspect_ratio);
  } };
  start.arrive_and_wait();
  REQUIRE(worker.CompletedFrameCount() == 0U);
  barrier.Release();
  first.join();
  second.join();
  REQUIRE(producer_ok.at(0));
  REQUIRE(producer_ok.at(1));
  REQUIRE(worker.WaitIdle());
  REQUIRE(worker.CompletedFrameCount()
          == static_cast<vkgsplat::u64>(kInitialFrameCount + (kProducerCount * kFramesPerProducer)));
  worker.Stop();
}

TEST_CASE("render thread propagates worker failure to blocked producers", "[render]")
{
  FrameBarrier barrier;
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless, [&barrier]() -> std::expected<void, vkgsplat::Error> {
    barrier.Wait();
    return std::unexpected{ vkgsplat::MakeError(std::errc::io_error, "injected frame failure") };
  });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  auto const aspect_ratio = camera.aspect_ratio();
  REQUIRE(worker.SubmitFrame(camera, aspect_ratio));
  barrier.WaitUntilEntered();
  REQUIRE(worker.SubmitFrame(camera, aspect_ratio));

  bool producer_failed = false;
  std::jthread producer{ [&]() -> void {
    auto result = worker.SubmitFrame(camera, aspect_ratio);
    producer_failed = !result && result.error().code() == std::make_error_code(std::errc::io_error);
  } };
  worker.WaitForBlockedProducerCountForTest(1U);
  barrier.Release();
  producer.join();
  REQUIRE(producer_failed);
  REQUIRE_FALSE(worker.WaitIdle());
  worker.Stop();
}

TEST_CASE("render thread shutdown wakes a blocked producer", "[render]")
{
  FrameBarrier barrier;
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless, [&barrier]() -> std::expected<void, vkgsplat::Error> {
    barrier.Wait();
    return {};
  });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  auto const aspect_ratio = camera.aspect_ratio();
  REQUIRE(worker.SubmitFrame(camera, aspect_ratio));
  barrier.WaitUntilEntered();
  REQUIRE(worker.SubmitFrame(camera, aspect_ratio));

  bool producer_failed = false;
  std::jthread producer{ [&]() -> void {
    auto result = worker.SubmitFrame(camera, aspect_ratio);
    producer_failed = !result && result.error().code() == std::make_error_code(std::errc::operation_not_permitted);
  } };
  worker.WaitForBlockedProducerCountForTest(1U);
  std::jthread stopper{ [&]() -> void { worker.Stop(); } };
  worker.WaitForShutdownForTest();
  barrier.Release();
  producer.join();
  stopper.join();
  REQUIRE(producer_failed);
}

TEST_CASE("lossless submission waits for an active idle command", "[render]")
{
  FrameBarrier idle_barrier;
  vkgsplat::RenderThread worker;
  worker.StartForTest(
    vkgsplat::FrameSubmissionPolicy::kLossless,
    []() -> std::expected<void, vkgsplat::Error> { return {}; },
    [&idle_barrier]() -> void { idle_barrier.Wait(); });

  bool idle_succeeded = false;
  std::jthread idle_waiter{ [&]() -> void { idle_succeeded = worker.WaitIdle().has_value(); } };
  idle_barrier.WaitUntilEntered();

  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  bool producer_succeeded = false;
  std::jthread producer{ [&]() -> void {
    producer_succeeded = worker.SubmitFrame(camera, camera.aspect_ratio()).has_value();
  } };
  worker.WaitForBlockedProducerCountForTest(1U);
  REQUIRE(worker.CompletedFrameCount() == 0U);
  idle_barrier.Release();
  idle_waiter.join();
  producer.join();
  REQUIRE(idle_succeeded);
  REQUIRE(producer_succeeded);
  REQUIRE(worker.WaitIdle());
  REQUIRE(worker.CompletedFrameCount() == 1U);
  worker.Stop();
}

TEST_CASE("shutdown during an idle command terminates the worker and fails the idle wait", "[render]")
{
  FrameBarrier idle_barrier;
  vkgsplat::RenderThread worker;
  worker.StartForTest(
    vkgsplat::FrameSubmissionPolicy::kLossless,
    []() -> std::expected<void, vkgsplat::Error> { return {}; },
    [&idle_barrier]() -> void { idle_barrier.Wait(); });

  bool idle_succeeded = true;
  std::jthread idle_waiter{ [&]() -> void { idle_succeeded = worker.WaitIdle().has_value(); } };
  idle_barrier.WaitUntilEntered();

  bool stop_completed = false;
  std::jthread stopper{ [&]() -> void {
    worker.Stop();
    stop_completed = true;
  } };
  worker.WaitForShutdownForTest();
  idle_barrier.Release();
  idle_waiter.join();
  stopper.join();
  REQUIRE(stop_completed);
  REQUIRE_FALSE(idle_succeeded);
}

TEST_CASE("CPU-only worker rejects GPU readback without a callback", "[render]")
{
  vkgsplat::RenderThread worker;
  worker.StartForTest(
    vkgsplat::FrameSubmissionPolicy::kLossless, []() -> std::expected<void, vkgsplat::Error> { return {}; });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  REQUIRE(worker.WaitIdle());
  auto frame = worker.ReadLatestFrame();
  REQUIRE_FALSE(frame);
  REQUIRE(frame.error().code() == std::errc::operation_not_supported);
  worker.Stop();
}

TEST_CASE("readback rejects an invalidated color target", "[render]")
{
  vkgsplat::RenderThread worker;
  worker.StartForTest(
    vkgsplat::FrameSubmissionPolicy::kLossless,
    []() -> std::expected<void, vkgsplat::Error> { return {}; },
    {},
    []() -> std::expected<vkgsplat::RgbaFrame, vkgsplat::Error> {
      return vkgsplat::RgbaFrame{ .width = kReadbackTestWidth,
        .height = kReadbackTestHeight,
        .pixels = { kReadbackRed, kReadbackGreen, kReadbackBlue, kReadbackAlpha } };
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  REQUIRE(worker.WaitIdle());
  REQUIRE(worker.ReadLatestFrame());
  worker.InvalidateColorTarget();
  auto stale = worker.ReadLatestFrame();
  REQUIRE_FALSE(stale);
  REQUIRE(stale.error().code() == std::errc::invalid_argument);
  worker.Stop();
}

TEST_CASE("readback blocks new submissions until its worker operation completes", "[render]")
{
  FrameBarrier draw_barrier;
  FrameBarrier readback_barrier;
  std::atomic<int> draw_count{ 0 };
  vkgsplat::RenderThread worker;
  worker.StartForTest(
    vkgsplat::FrameSubmissionPolicy::kLossless,
    [&]() -> std::expected<void, vkgsplat::Error> {
      if (draw_count.fetch_add(1) == 0) { draw_barrier.Wait(); }
      return {};
    },
    {},
    [&]() -> std::expected<vkgsplat::RgbaFrame, vkgsplat::Error> {
      readback_barrier.Wait();
      return vkgsplat::RgbaFrame{ .width = kReadbackTestWidth,
        .height = kReadbackTestHeight,
        .pixels = { kReadbackRed, kReadbackGreen, kReadbackBlue, kReadbackAlpha } };
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  draw_barrier.WaitUntilEntered();

  std::optional<std::expected<vkgsplat::RgbaFrame, vkgsplat::Error>> readback_result;
  std::jthread reader{ [&]() -> void { readback_result = worker.ReadLatestFrame(); } };
  worker.WaitForReadbackPendingForTest();
  std::atomic<bool> producer_finished{ false };
  bool producer_succeeded = false;
  std::jthread producer{ [&]() -> void {
    producer_succeeded = worker.SubmitFrame(camera, camera.aspect_ratio()).has_value();
    producer_finished.store(true);
  } };
  worker.WaitForBlockedProducerCountForTest(1U);
  draw_barrier.Release();
  readback_barrier.WaitUntilEntered();
  CHECK_FALSE(producer_finished.load());
  readback_barrier.Release();
  reader.join();
  producer.join();

  REQUIRE(readback_result.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  REQUIRE(readback_result->has_value());
  std::vector<std::uint8_t> const expected_pixels{ kReadbackRed, kReadbackGreen, kReadbackBlue, kReadbackAlpha };
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  REQUIRE(readback_result->value().pixels == expected_pixels);
  REQUIRE(producer_succeeded);
  REQUIRE(worker.WaitIdle());
  REQUIRE(worker.CompletedFrameCount() == kTwoFrameCount);
  worker.Stop();
}

TEST_CASE("queued screenshot survives ordinary submissions in latest mode", "[render]")
{
  FrameBarrier draw_barrier;
  std::atomic<int> draw_count{ 0 };
  std::atomic<int> readback_count{ 0 };
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLatest,
    [&]() -> std::expected<void, vkgsplat::Error> {
      if (draw_count.fetch_add(1) == 0) { draw_barrier.Wait(); }
      return {};
    }, {},
    [&]() -> std::expected<vkgsplat::RgbaFrame, vkgsplat::Error> {
      ++readback_count;
      return vkgsplat::RgbaFrame{ .width = kReadbackTestWidth,
        .height = kReadbackTestHeight,
        .pixels = { kReadbackRed, kReadbackGreen, kReadbackBlue, kReadbackAlpha } };
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  draw_barrier.WaitUntilEntered();

  std::optional<std::expected<vkgsplat::RgbaFrame, vkgsplat::Error>> captured;
  std::jthread reader{ [&]() -> void { captured = worker.CaptureFrame(camera, camera.aspect_ratio()); } };
  worker.WaitForCapturePendingForTest();
  bool producer_succeeded = false;
  std::jthread producer{ [&]() -> void {
    producer_succeeded = worker.SubmitFrame(camera, camera.aspect_ratio()).has_value();
  } };
  worker.WaitForBlockedProducerCountForTest(1U);
  draw_barrier.Release();
  reader.join();
  producer.join();
  REQUIRE(captured.has_value());
  REQUIRE(captured->has_value());
  std::vector<std::uint8_t> const expected_pixels{ kReadbackRed, kReadbackGreen, kReadbackBlue, kReadbackAlpha };
  REQUIRE(captured->value().pixels == expected_pixels);
  REQUIRE(producer_succeeded);
  REQUIRE(readback_count.load() == 1);
  worker.Stop();
}

TEST_CASE("shutdown cancels a queued screenshot and rejects later submissions", "[render]")
{
  FrameBarrier draw_barrier;
  std::atomic<int> readback_count{ 0 };
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless,
    [&]() -> std::expected<void, vkgsplat::Error> {
      draw_barrier.Wait();
      return {};
    }, {},
    [&]() -> std::expected<vkgsplat::RgbaFrame, vkgsplat::Error> {
      ++readback_count;
      return vkgsplat::RgbaFrame{ .width = kReadbackTestWidth,
        .height = kReadbackTestHeight,
        .pixels = { kReadbackRed, kReadbackGreen, kReadbackBlue, kReadbackAlpha } };
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  draw_barrier.WaitUntilEntered();

  std::optional<std::expected<vkgsplat::RgbaFrame, vkgsplat::Error>> captured;
  std::jthread reader{ [&]() -> void { captured = worker.CaptureFrame(camera, camera.aspect_ratio()); } };
  worker.WaitForCapturePendingForTest();
  std::jthread stopper{ [&]() -> void { worker.Stop(); } };
  worker.WaitForShutdownForTest();
  draw_barrier.Release();
  reader.join();
  stopper.join();
  REQUIRE(captured.has_value());
  REQUIRE_FALSE(captured->has_value());
  REQUIRE(captured->error().code() == std::errc::operation_canceled);
  REQUIRE(readback_count.load() == 0);
  auto submitted = worker.SubmitFrame(camera, camera.aspect_ratio());
  REQUIRE_FALSE(submitted);
  REQUIRE(submitted.error().code() == std::errc::operation_not_permitted);
}

TEST_CASE("shutdown waits for executing screenshot readback", "[render]")
{
  FrameBarrier readback_barrier;
  std::atomic<bool> stop_returned{ false };
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless,
    []() -> std::expected<void, vkgsplat::Error> { return {}; }, {},
    [&]() -> std::expected<vkgsplat::RgbaFrame, vkgsplat::Error> {
      readback_barrier.Wait();
      return vkgsplat::RgbaFrame{ .width = kReadbackTestWidth,
        .height = kReadbackTestHeight,
        .pixels = { kReadbackRed, kReadbackGreen, kReadbackBlue, kReadbackAlpha } };
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  std::optional<std::expected<vkgsplat::RgbaFrame, vkgsplat::Error>> captured;
  std::jthread reader{ [&]() -> void { captured = worker.CaptureFrame(camera, camera.aspect_ratio()); } };
  readback_barrier.WaitUntilEntered();
  std::jthread stopper{ [&]() -> void {
    worker.Stop();
    stop_returned.store(true);
  } };
  worker.WaitForShutdownForTest();
  CHECK_FALSE(stop_returned.load());
  readback_barrier.Release();
  reader.join();
  stopper.join();
  REQUIRE(captured.has_value());
  REQUIRE(captured->has_value());
  REQUIRE(stop_returned.load());
}

TEST_CASE("queued video capture is canceled after shutdown", "[render]")
{
  FrameBarrier draw_barrier;
  std::atomic<int> encoded_count{ 0 };
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLatest,
    [&]() -> std::expected<void, vkgsplat::Error> {
      draw_barrier.Wait();
      return {};
    }, {}, {},
    [&]() -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error> {
      ++encoded_count;
      return vkgsplat::EncodedPacket{};
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  draw_barrier.WaitUntilEntered();
  std::optional<std::expected<vkgsplat::EncodedPacket, vkgsplat::Error>> captured;
  std::jthread recorder{ [&]() -> void {
    captured = worker.CaptureVideoFrameForTest(camera, camera.aspect_ratio(), 0U);
  } };
  worker.WaitForVideoCapturePendingForTest();
  std::jthread stopper{ [&]() -> void { worker.Stop(); } };
  worker.WaitForShutdownForTest();
  draw_barrier.Release();
  recorder.join();
  stopper.join();
  REQUIRE(captured.has_value());
  REQUIRE_FALSE(captured->has_value());
  REQUIRE(captured->error().code() == std::errc::operation_canceled);
  REQUIRE(encoded_count.load() == 0);
}

TEST_CASE("queued video capture blocks ordinary submissions in latest mode", "[render]")
{
  FrameBarrier draw_barrier;
  std::atomic<int> draw_count{ 0 };
  std::atomic<int> encoded_count{ 0 };
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLatest,
    [&]() -> std::expected<void, vkgsplat::Error> {
      if (draw_count.fetch_add(1) == 0) { draw_barrier.Wait(); }
      return {};
    }, {}, {},
    [&]() -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error> {
      ++encoded_count;
      return vkgsplat::EncodedPacket{};
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  draw_barrier.WaitUntilEntered();
  std::optional<std::expected<vkgsplat::EncodedPacket, vkgsplat::Error>> captured;
  std::jthread recorder{ [&]() -> void {
    captured = worker.CaptureVideoFrameForTest(camera, camera.aspect_ratio(), 0U);
  } };
  worker.WaitForVideoCapturePendingForTest();
  bool producer_succeeded = false;
  std::jthread producer{ [&]() -> void {
    producer_succeeded = worker.SubmitFrame(camera, camera.aspect_ratio()).has_value();
  } };
  worker.WaitForBlockedProducerCountForTest(1U);
  draw_barrier.Release();
  recorder.join();
  producer.join();
  REQUIRE(captured.has_value());
  REQUIRE(captured->has_value());
  REQUIRE(producer_succeeded);
  REQUIRE(encoded_count.load() == 1);
  worker.Stop();
}

TEST_CASE("shutdown waits for executing video capture", "[render]")
{
  FrameBarrier encode_barrier;
  std::atomic<bool> stop_returned{ false };
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless,
    []() -> std::expected<void, vkgsplat::Error> { return {}; }, {}, {},
    [&]() -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error> {
      encode_barrier.Wait();
      return vkgsplat::EncodedPacket{};
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  std::optional<std::expected<vkgsplat::EncodedPacket, vkgsplat::Error>> captured;
  std::jthread recorder{ [&]() -> void {
    captured = worker.CaptureVideoFrameForTest(camera, camera.aspect_ratio(), 0U);
  } };
  encode_barrier.WaitUntilEntered();
  std::jthread stopper{ [&]() -> void {
    worker.Stop();
    stop_returned.store(true);
  } };
  worker.WaitForShutdownForTest();
  CHECK_FALSE(stop_returned.load());
  encode_barrier.Release();
  recorder.join();
  stopper.join();
  REQUIRE(captured.has_value());
  REQUIRE(captured->has_value());
  REQUIRE(stop_returned.load());
}

TEST_CASE("video device loss is terminal to later frame submissions", "[render]")
{
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless,
    []() -> std::expected<void, vkgsplat::Error> { return {}; }, {}, {},
    []() -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error> {
      return std::unexpected{ vkgsplat::MakeError(std::errc::no_such_device, "injected device loss") };
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  auto captured = worker.CaptureVideoFrameForTest(camera, camera.aspect_ratio(), 0U);
  REQUIRE_FALSE(captured);
  REQUIRE(captured.error().code() == std::errc::no_such_device);
  auto submitted = worker.SubmitFrame(camera, camera.aspect_ratio());
  REQUIRE_FALSE(submitted);
  REQUIRE(submitted.error().code() == std::errc::no_such_device);
  worker.Stop();
}

TEST_CASE("vkexec submission errors retain structured device loss", "[render]")
{
  vkexec::error const failure{
    .code = vkexec::make_vk_error_code(VK_ERROR_DEVICE_LOST),
    .detail = "injected queue submission failure",
  };
  auto converted = vkgsplat::vulkan::VkexecFailure(failure);
  REQUIRE(converted.code() == std::errc::no_such_device);
}

TEST_CASE("recording-local failure releases video admission without stopping rendering", "[render]")
{
  vkgsplat::RenderThread worker;
  std::atomic<int> encode_count{ 0 };
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless,
    []() -> std::expected<void, vkgsplat::Error> { return {}; }, {}, {},
    [&]() -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error> {
      ++encode_count;
      return std::unexpected{ vkgsplat::MakeError(std::errc::io_error, "injected encode failure") };
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  auto captured = worker.CaptureVideoFrameForTest(camera, camera.aspect_ratio(), 0U);
  REQUIRE_FALSE(captured);
  REQUIRE(captured.error().code() == std::errc::io_error);
  REQUIRE(encode_count.load() == 1);
  REQUIRE_FALSE(worker.PollError().has_value());
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  REQUIRE(worker.WaitIdle());
  worker.Stop();
}

TEST_CASE("frame failure resolves a queued screenshot and blocked producer", "[render]")
{
  FrameBarrier draw_barrier;
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless,
    [&]() -> std::expected<void, vkgsplat::Error> {
      draw_barrier.Wait();
      return std::unexpected{ vkgsplat::MakeError(std::errc::io_error, "injected draw failure") };
    }, {},
    []() -> std::expected<vkgsplat::RgbaFrame, vkgsplat::Error> {
      return vkgsplat::RgbaFrame{};
    });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  draw_barrier.WaitUntilEntered();
  std::optional<std::expected<vkgsplat::RgbaFrame, vkgsplat::Error>> captured;
  std::jthread reader{ [&]() -> void { captured = worker.CaptureFrame(camera, camera.aspect_ratio()); } };
  worker.WaitForCapturePendingForTest();
  std::optional<std::expected<void, vkgsplat::Error>> submitted;
  std::jthread producer{ [&]() -> void { submitted = worker.SubmitFrame(camera, camera.aspect_ratio()); } };
  worker.WaitForBlockedProducerCountForTest(1U);
  draw_barrier.Release();
  reader.join();
  producer.join();
  REQUIRE(captured.has_value());
  REQUIRE_FALSE(captured->has_value());
  REQUIRE(captured->error().code() == std::errc::io_error);
  REQUIRE(submitted.has_value());
  REQUIRE_FALSE(submitted->has_value());
  REQUIRE(submitted->error().code() == std::errc::io_error);
  worker.Stop();
}

TEST_CASE("exclusive reconfiguration blocks submissions until the worker completes it", "[render]")
{
  FrameBarrier draw_barrier;
  FrameBarrier exclusive_barrier;
  std::atomic<int> draw_count{ 0 };
  vkgsplat::RenderThread worker;
  worker.StartForTest(vkgsplat::FrameSubmissionPolicy::kLossless, [&]() -> std::expected<void, vkgsplat::Error> {
    if (draw_count.fetch_add(1) == 0) { draw_barrier.Wait(); }
    return {};
  });
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(worker.SubmitFrame(camera, camera.aspect_ratio()));
  draw_barrier.WaitUntilEntered();

  std::optional<std::expected<void, vkgsplat::Error>> exclusive_result;
  std::jthread reconfiguration{ [&]() -> void {
    exclusive_result = worker.RunExclusive([&]() -> std::expected<void, vkgsplat::Error> {
      exclusive_barrier.Wait();
      return {};
    });
  } };
  worker.WaitForExclusivePendingForTest();
  std::atomic<bool> producer_finished{ false };
  bool producer_succeeded = false;
  std::jthread producer{ [&]() -> void {
    producer_succeeded = worker.SubmitFrame(camera, camera.aspect_ratio()).has_value();
    producer_finished.store(true);
  } };
  worker.WaitForBlockedProducerCountForTest(1U);
  draw_barrier.Release();
  exclusive_barrier.WaitUntilEntered();
  CHECK_FALSE(producer_finished.load());
  exclusive_barrier.Release();
  reconfiguration.join();
  producer.join();

  REQUIRE(exclusive_result.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  REQUIRE(exclusive_result->has_value());
  REQUIRE(producer_succeeded);
  REQUIRE(worker.WaitIdle());
  REQUIRE(worker.CompletedFrameCount() == kTwoFrameCount);
  worker.Stop();
}
// NOLINTEND(readability-function-cognitive-complexity)
