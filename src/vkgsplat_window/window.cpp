#include <vkgsplat_window/window.hpp>

#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <expected>
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

void Window::wait_events() const noexcept
{
  (void)impl_;
  glfwWaitEvents();
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
