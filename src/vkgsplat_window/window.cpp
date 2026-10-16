#include <vkgsplat_window/window.hpp>

#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <beman/indirect/indirect.hpp>

#include <GLFW/glfw3.h>

namespace vkgsplat {

struct Window::Impl
{
  GLFWwindow *handle = nullptr;
};

namespace {

  [[nodiscard]] auto MakeErrorFromGlfw(std::string message) -> Error
  {
    char const *glfw_message = nullptr;
    glfwGetError(&glfw_message);
    if (glfw_message != nullptr) {
      message += ": ";
      message += glfw_message;
    }
    return MakeError(std::errc::io_error, std::move(message));
  }

}// namespace

Window::Window(beman::indirect::indirect<Impl> impl) : impl_(std::move(impl)) {}

Window::Window(Window &&) noexcept = default;
auto Window::operator=(Window &&) noexcept -> Window & = default;

Window::~Window() noexcept
{
  if (impl_.valueless_after_move() || impl_->handle == nullptr) { return; }
  glfwDestroyWindow(std::exchange(impl_->handle, nullptr));
  glfwTerminate();
}

auto Window::create(WindowConfig const &config) -> std::expected<Window, Error>
{
  if (glfwInit() == 0) { return std::unexpected{ MakeErrorFromGlfw("Failed to initialize GLFW") }; }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

  auto *const handle = glfwCreateWindow(static_cast<int>(config.width),
    static_cast<int>(config.height),
    std::string(config.title.data(), config.title.size()).c_str(),
    nullptr,
    nullptr);
  if (handle == nullptr) {
    glfwTerminate();
    return std::unexpected{ MakeErrorFromGlfw("Failed to create GLFW window") };
  }

  beman::indirect::indirect<Impl> impl;
  impl->handle = handle;
  return Window{ std::move(impl) };
}

void Window::poll_events() const noexcept
{
  (void)impl_;
  glfwPollEvents();
}

auto Window::post_empty_event() const noexcept -> void
{
  (void)impl_;
  glfwPostEmptyEvent();
}

void Window::wait_events() const noexcept
{
  (void)impl_;
  glfwWaitEvents();
}

auto Window::wait_events_timeout(f64 seconds) const noexcept -> void
{
  (void)impl_;
  glfwWaitEventsTimeout(seconds);
}

// Keep the window clock in the instance API used by the event loop.
// cppcheck-suppress functionStatic
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto Window::time_seconds() const noexcept -> f64 { return glfwGetTime(); }

auto Window::refresh_rate_hz() const noexcept -> u32
{
  GLFWmonitor *monitor = glfwGetWindowMonitor(impl_->handle);
  if (monitor == nullptr) {
    int window_x = 0;
    int window_y = 0;
    int window_width = 0;
    int window_height = 0;
    glfwGetWindowPos(impl_->handle, &window_x, &window_y);
    glfwGetWindowSize(impl_->handle, &window_width, &window_height);

    int monitor_count = 0;
    GLFWmonitor **const monitors = glfwGetMonitors(&monitor_count);
    std::int64_t greatest_overlap = 0;
    if (monitors != nullptr && monitor_count > 0) {
      for (GLFWmonitor *candidate : std::span{ monitors, static_cast<size_t>(monitor_count) }) {
        GLFWvidmode const *const candidate_mode = glfwGetVideoMode(candidate);
        if (candidate_mode == nullptr) { continue; }
        int monitor_x = 0;
        int monitor_y = 0;
        glfwGetMonitorPos(candidate, &monitor_x, &monitor_y);
        auto const overlap_width = std::max(
          0, std::min(window_x + window_width, monitor_x + candidate_mode->width) - std::max(window_x, monitor_x));
        auto const overlap_height = std::max(
          0, std::min(window_y + window_height, monitor_y + candidate_mode->height) - std::max(window_y, monitor_y));
        auto const overlap = static_cast<std::int64_t>(overlap_width) * overlap_height;
        if (overlap > greatest_overlap) {
          greatest_overlap = overlap;
          monitor = candidate;
        }
      }
    }
    if (monitor == nullptr) { monitor = glfwGetPrimaryMonitor(); }
  }
  if (monitor == nullptr) { return 0U; }
  GLFWvidmode const *const mode = glfwGetVideoMode(monitor);
  if (mode == nullptr || mode->refreshRate <= 0) { return 0U; }
  return static_cast<u32>(mode->refreshRate);
}

auto Window::should_close() const noexcept -> bool { return glfwWindowShouldClose(impl_->handle) != 0; }

auto Window::framebuffer_extent() const noexcept -> Extent2D
{
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(impl_->handle, &width, &height);
  return Extent2D{ .width = static_cast<u32>(width), .height = static_cast<u32>(height) };
}

auto Window::native_window() const noexcept -> NativeWindowHandle { return impl_->handle; }

auto Window::native_handle() const noexcept -> void * { return impl_->handle; }

}// namespace vkgsplat
