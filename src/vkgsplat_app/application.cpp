#include "application.hpp"

#include <exception>
#include <print>
#include <span>
#include <system_error>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_input_handler/input_handler.hpp>
#include <vkgsplat_utility/input_control.hpp>
#include <vkgsplat_window/window.hpp>

#include "app_config.hpp"

namespace vkgsplat::app {

auto Run(std::span<char *const> args) noexcept -> int
{
  try {
    auto const config = ParseAppConfig(args);
    if (!config) {
      if (config.error().code() == std::make_error_code(std::errc::operation_canceled)) { return 0; }
      std::println(
        stderr, "Failed to parse app config ({}): {}", config.error().code().value(), config.error().message());
      return -1;
    }

    auto window = vkgsplat::Window::create(vkgsplat::WindowConfig{ .title = "vkgsplat" });
    if (!window) {
      std::println(stderr, "Failed to create window: {}", window.error().message());
      return -1;
    }

    vkgsplat::RendererConfig const renderer_config{
      .splat_count = config->splat_count,
      .ply_path = config->ply_path,
      .enable_validation = config->enable_validation,
      .enable_imgui = config->enable_imgui,
      .enable_gpu_timers = config->enable_gpu_timers,
    };

    auto renderer = vkgsplat::Renderer::create(renderer_config, *window);
    if (!renderer) {
      std::println(stderr, "Failed to create renderer: {}", renderer.error().message());
      return -1;
    }

    vkgsplat::Camera camera{ vkgsplat::kDefaultCameraPosition };
    vkgsplat::CloseState close{};
    vkgsplat::InputHandler input{ *window };

    while (!window->should_close() && !close.close_requested()) {
      window->poll_events();
      input.update(camera, close);
      auto const draw_result = renderer->draw(camera);
      if (!draw_result) {
        std::println(stderr, "failed to draw frame: {}", draw_result.error().message());
        return -1;
      }
    }

    renderer->wait_idle();
  } catch (std::exception const &) {
    return -1;
  } catch (...) {
    return -1;
  }
  return 0;
}

}// namespace vkgsplat::app
