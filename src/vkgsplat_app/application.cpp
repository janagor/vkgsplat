#include "application.hpp"

#include <chrono>
#include <exception>
#include <format>
#include <print>
#include <span>
#include <string>
#include <system_error>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_input_handler/input_handler.hpp>
#include <vkgsplat_utility/input_control.hpp>
#include <vkgsplat_window/window.hpp>

#include "app_config.hpp"

namespace vkgsplat::app {

namespace {

  [[nodiscard]] auto MakeScreenshotPath() -> std::string
  {
    auto const now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("screenshot_{:%Y%m%d_%H%M%S}.png", now);
  }

}// namespace

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

    constexpr auto kDefaultConfig = WindowConfig{
      .title = "vkgsplat",
      .width = 1024,
      .height = 1024,
      .resizable = true,
    };


    auto window = vkgsplat::Window::create(kDefaultConfig);
    if (!window) {
      std::println(stderr, "Failed to create window: {}", window.error().message());
      return -1;
    }

    vkgsplat::RendererConfig const renderer_config{
      .ply_path = config->ply_path,
      .enable_validation = config->enable_validation,
      .enable_imgui = config->enable_imgui,
      .enable_gpu_timers = config->enable_gpu_timers,
      .frame_rate = config->frame_rate,
    };

    auto renderer = vkgsplat::Renderer::create(renderer_config, *window);
    if (!renderer) {
      std::println(stderr, "Failed to create renderer: {}", renderer.error().message());
      return -1;
    }

    vkgsplat::Camera camera{
      glm::dvec3{ config->camera_position.at(0), config->camera_position.at(1), config->camera_position.at(2) },
      glm::dvec3{ config->camera_target.at(0), config->camera_target.at(1), config->camera_target.at(2) },
    };
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

      if (input.consume_screenshot_request()) {
        auto const path = MakeScreenshotPath();
        if (auto saved = renderer->save_frame_png(path); !saved) {
          std::println(stderr, "failed to save screenshot: {}", saved.error().message());
        } else {
          std::println("saved screenshot: {}", path);
        }
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
