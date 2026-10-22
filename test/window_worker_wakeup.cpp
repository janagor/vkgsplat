// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_window/window.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

TEST_CASE("worker notification wakes an idle GLFW event wait", "[window]")
{
  constexpr auto kWorkerDelay = std::chrono::milliseconds{ 20 };
  constexpr auto kMaximumWakeTime = std::chrono::seconds{ 1 };
  constexpr vkgsplat::u32 kWindowExtent = 64U;
  auto window = vkgsplat::Window::create(
    vkgsplat::WindowConfig{ .title = "worker wakeup", .width = kWindowExtent, .height = kWindowExtent });
  if (!window.has_value()) {
    SKIP("GLFW window unavailable in this environment");
    return;
  }

  window->poll_events();
  std::atomic<bool> posted = false;
  std::jthread const worker{ [&]() -> void {
    std::this_thread::sleep_for(kWorkerDelay);
    posted.store(true);
    window->post_empty_event();
  } };

  auto const start = std::chrono::steady_clock::now();
  while (!posted.load()) { window->wait_events(); }
  REQUIRE(std::chrono::steady_clock::now() - start < kMaximumWakeTime);
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
