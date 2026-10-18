// NOLINTBEGIN(readability-function-cognitive-complexity)
#include "vulkan/render_thread.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <barrier>
#include <condition_variable>
#include <expected>
#include <mutex>
#include <system_error>
#include <thread>

namespace {

inline constexpr int kProducerCount = 2;
inline constexpr int kFramesPerProducer = 100;
inline constexpr int kInitialFrameCount = 1;

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
// NOLINTEND(readability-function-cognitive-complexity)
