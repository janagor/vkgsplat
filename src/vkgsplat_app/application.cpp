#include "application.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <expected>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <system_error>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/frame_rate.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_capture/screenshot.hpp>
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

  auto HandleLfdEmulateInput(Renderer &renderer, InputHandler &input) -> std::expected<bool, Error>
  {
    bool changed = false;

    if (input.consume_emulate_toggle()) {
      if (auto toggled = renderer.set_lfd_emulate(!renderer.lfd_emulate_active()); !toggled) {
        return std::unexpected{ toggled.error() };
      }
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
      case InputHandler::ArrowDir::kNone:
        break;
      }
      if (auto selected = renderer.set_lfd_emulate_cell(cell.at(0), cell.at(1)); !selected) {
        return std::unexpected{ selected.error() };
      }
      changed = true;
    }

    return changed;
  }

  void MaybeSaveScreenshot(Renderer &renderer, InputHandler &input)
  {
    if (!input.consume_screenshot_request()) { return; }
    auto const path = MakeScreenshotPath();
    if (auto saved = capture::SavePng(renderer, path); !saved) {
      std::println(stderr, "failed to save screenshot: {}", saved.error().message());
    } else {
      std::println("saved screenshot: {}", path);
    }
  }

  struct MainLoopSchedule
  {
    f64 input_interval{};
    std::optional<f64> cpu_render_interval;
    f64 next_input_time{};
    f64 next_render_time{};
    u64 last_completed_frame_count{};
    u64 last_recreated_frame_count{};
    Extent2D last_extent{};
    bool redraw_needed{ true };
  };

  auto UpdateRenderDemand(Window const &window,
    Renderer const &renderer,
    InputHandler &input,
    Camera &camera,
    CloseState &close,
    MainLoopSchedule &schedule) -> void
  {
    window.poll_events();
    if (input.update(camera, close)) { schedule.redraw_needed = true; }

    auto const completed_frame_count = renderer.completed_frame_count();
    if (completed_frame_count != schedule.last_completed_frame_count) {
      schedule.last_completed_frame_count = completed_frame_count;
      if (input.movement_active() && !schedule.cpu_render_interval.has_value()) { schedule.redraw_needed = true; }
    }

    auto const recreated_frame_count = renderer.recreated_frame_count();
    if (recreated_frame_count != schedule.last_recreated_frame_count) {
      schedule.last_recreated_frame_count = recreated_frame_count;
      schedule.redraw_needed = true;
    }

    auto const extent = window.framebuffer_extent();
    if (extent.width != schedule.last_extent.width || extent.height != schedule.last_extent.height) {
      schedule.last_extent = extent;
      schedule.redraw_needed = true;
    }
  }

  [[nodiscard]] auto
    SubmitDueFrame(Window const &window, Renderer &renderer, Camera const &camera, MainLoopSchedule &schedule)
      -> std::expected<void, Error>
  {
    auto const now = window.time_seconds();
    if (!schedule.redraw_needed || (schedule.cpu_render_interval.has_value() && now < schedule.next_render_time)) {
      return {};
    }
    if (auto drawn = renderer.draw(camera); !drawn) { return std::unexpected{ drawn.error() }; }
    schedule.redraw_needed = false;
    if (schedule.cpu_render_interval.has_value()) {
      schedule.next_render_time = now + schedule.cpu_render_interval.value();
    }
    return {};
  }

  auto WaitForNextWork(Window const &window, InputHandler const &input, MainLoopSchedule &schedule) -> void
  {
    auto const wait_start = window.time_seconds();
    if (input.movement_active() && schedule.next_input_time <= wait_start) {
      schedule.next_input_time += schedule.input_interval;
      if (schedule.next_input_time <= wait_start) { schedule.next_input_time = wait_start + schedule.input_interval; }
    }
    if (!input.movement_active() && !schedule.redraw_needed && !input.screenshot_requested()) {
      window.wait_events();
      return;
    }

    f64 deadline = schedule.next_input_time;
    if (schedule.redraw_needed && schedule.cpu_render_interval.has_value()) {
      deadline = input.movement_active() ? std::min(schedule.next_input_time, schedule.next_render_time)
                                         : schedule.next_render_time;
    }
    if (deadline > wait_start) { window.wait_events_timeout(deadline - wait_start); }
  }

  [[nodiscard]] auto RunMainLoop(Window &window,
    Renderer &renderer,
    CameraConfig const &camera_config,
    KeyBindings const &key_bindings,
    FrameRateConfig const &frame_rate,
    u32 configured_input_rate_hz) -> std::expected<void, Error>
  {
    constexpr u32 kMinimumInputRateHz = 120U;
    auto const input_rate_hz = configured_input_rate_hz > 0U ? configured_input_rate_hz
                                                             : std::max(kMinimumInputRateHz, window.refresh_rate_hz());
    auto const input_interval = 1.0 / static_cast<f64>(input_rate_hz);
    std::optional<f64> cpu_render_interval{};
    if (frame_rate.mode == FrameRateMode::kFixed && frame_rate.fixed_fps > 0U) {
      cpu_render_interval = 1.0 / static_cast<f64>(frame_rate.fixed_fps);
    }
    Camera camera{ camera_config };
    CloseState close{};
    InputHandler input{ window, key_bindings };

    MainLoopSchedule schedule{
      .input_interval = input_interval,
      .cpu_render_interval = cpu_render_interval,
      .next_input_time = window.time_seconds() + input_interval,
      .next_render_time = window.time_seconds(),
      .last_completed_frame_count = renderer.completed_frame_count(),
      .last_recreated_frame_count = renderer.recreated_frame_count(),
      .last_extent = window.framebuffer_extent(),
    };

    while (!window.should_close() && !close.close_requested()) {
      // Poll first so held keys are visible; only block when nothing needs a frame.
      UpdateRenderDemand(window, renderer, input, camera, close, schedule);
      if (auto worker_error = renderer.poll_error(); worker_error.has_value()) {
        return std::unexpected{ worker_error.value() };
      }
      if (auto drawn = SubmitDueFrame(window, renderer, camera, schedule); !drawn) {
        return std::unexpected{ drawn.error() };
      }

      MaybeSaveScreenshot(renderer, input);

      auto lfd_input = HandleLfdEmulateInput(renderer, input);
      if (!lfd_input) { return std::unexpected{ lfd_input.error() }; }
      if (*lfd_input) { schedule.redraw_needed = true; }
      WaitForNextWork(window, input, schedule);
      if (auto worker_error = renderer.poll_error(); worker_error.has_value()) {
        return std::unexpected{ worker_error.value() };
      }
    }

    if (auto idle = renderer.wait_idle(); !idle) { return std::unexpected{ idle.error() }; }
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
      .quilt_size = config->quilt_size,
      .view_cone_deg = config->view_cone_deg,
      .lfd_view_order = config->lfd_view_order,
    };

    auto renderer = Renderer::create(renderer_config, *window);
    if (!renderer) {
      std::println(stderr, "Failed to create renderer: {}", renderer.error().message());
      return -1;
    }

    if (auto loop = RunMainLoop(
          *window, *renderer, config->camera, config->key_bindings, config->frame_rate, config->input_update_hz);
      !loop) {
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
