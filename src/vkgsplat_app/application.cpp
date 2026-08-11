#include "application.hpp"

#include <chrono>
#include <exception>
#include <expected>
#include <format>
#include <print>
#include <span>
#include <string>
#include <system_error>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_input_handler/input_handler.hpp>
#include <vkgsplat_input_handler/key_bindings.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/input_control.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_window/window.hpp>

#include "app_config.hpp"

namespace vkgsplat::app {

namespace {

  [[nodiscard]] auto MakeScreenshotPath() -> std::string
  {
    auto const now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("screenshot_{:%Y%m%d_%H%M%S}.png", now);
  }

  auto HandleLfdEmulateInput(Renderer &renderer, InputHandler &input) -> bool
  {
    bool changed = false;

    if (input.consume_emulate_toggle()) {
      renderer.set_lfd_emulate(!renderer.lfd_emulate_active());
      changed = true;
    }

    if (auto const arrow = input.consume_arrow();
        arrow != InputHandler::ArrowDir::kNone && renderer.lfd_emulate_active()) {
      auto cell = renderer.lfd_emulate_cell();
      auto const grid = renderer.lfd_grid();
      switch (arrow) {
      case InputHandler::ArrowDir::kLeft:
        if (cell.at(0) > 0U) { --cell.at(0); }
        break;
      case InputHandler::ArrowDir::kRight:
        if (cell.at(0) + 1U < grid.at(0)) { ++cell.at(0); }
        break;
      case InputHandler::ArrowDir::kUp:
        if (cell.at(1) > 0U) { --cell.at(1); }
        break;
      case InputHandler::ArrowDir::kDown:
        if (cell.at(1) + 1U < grid.at(1)) { ++cell.at(1); }
        break;
      case InputHandler::ArrowDir::kNone: break;
      }
      renderer.set_lfd_emulate_cell(cell.at(0), cell.at(1));
      changed = true;
    }

    return changed;
  }

  void MaybeSaveScreenshot(Renderer &renderer, InputHandler &input)
  {
    if (!input.consume_screenshot_request()) { return; }
    auto const path = MakeScreenshotPath();
    if (auto saved = renderer.save_frame_png(path); !saved) {
      std::println(stderr, "failed to save screenshot: {}", saved.error().message());
    } else {
      std::println("saved screenshot: {}", path);
    }
  }

  [[nodiscard]] auto RunMainLoop(Window &window,
    Renderer &renderer,
    CameraConfig const &camera_config,
    KeyBindings const &key_bindings) -> std::expected<void, Error>
  {
    Camera camera{ camera_config };
    CloseState close{};
    InputHandler input{ window, key_bindings };

    bool redraw_needed = true;
    Extent2D last_extent = window.framebuffer_extent();

    while (!window.should_close() && !close.close_requested()) {
      // Poll first so held keys are visible; only block when nothing needs a frame.
      window.poll_events();

      if (input.update(camera, close)) { redraw_needed = true; }

      auto const extent = window.framebuffer_extent();
      if (extent.width != last_extent.width || extent.height != last_extent.height) {
        last_extent = extent;
        redraw_needed = true;
      }

      if (redraw_needed) {
        if (auto draw_result = renderer.draw(camera); !draw_result) { return std::unexpected{ draw_result.error() }; }
        redraw_needed = false;
      } else if (!input.screenshot_requested()) {
        // CPU sleep until the next window/input event; next iteration polls + may draw.
        window.wait_events();
      }

      MaybeSaveScreenshot(renderer, input);

      if (HandleLfdEmulateInput(renderer, input)) { redraw_needed = true; }
    }

    renderer.wait_idle();
    return {};
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

    auto window = Window::create(kDefaultConfig);
    if (!window) {
      std::println(stderr, "Failed to create window: {}", window.error().message());
      return -1;
    }

    RendererConfig const renderer_config{
      .ply_path = config->ply_path,
      .enable_validation = config->enable_validation,
      .enable_imgui = config->enable_imgui,
      .enable_gpu_timers = config->enable_gpu_timers,
      .frame_rate = config->frame_rate,
      .lfd_grid = config->lfd_grid,
      .view_cone_deg = config->view_cone_deg,
      .lfd_view_order = config->lfd_view_order,
    };

    auto renderer = Renderer::create(renderer_config, *window);
    if (!renderer) {
      std::println(stderr, "Failed to create renderer: {}", renderer.error().message());
      return -1;
    }

    if (auto loop = RunMainLoop(*window, *renderer, config->camera, config->key_bindings); !loop) {
      std::println(stderr, "failed to draw frame: {}", loop.error().message());
      return -1;
    }
  } catch (std::exception const &) {
    return -1;
  } catch (...) {
    return -1;
  }
  return 0;
}

}// namespace vkgsplat::app
