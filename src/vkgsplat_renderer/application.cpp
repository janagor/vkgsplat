#include "application.hpp"

#include <exception>
#include <print>
#include <span>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>

#include "app_config.hpp"

namespace vkgsplat {

auto run(std::span<char *const> args) noexcept -> int
{
  try {
    auto const config = parse_app_config(args);
    if (!config) {
      std::println(stderr, "Failed to parse app config ({}): {}", config.error().code().value(), config.error().message());
      return -1;
    }

    RendererConfig const renderer_config{
      .source = config->source,
      .splat_count = config->splat_count,
      .ply_path = config->ply_path,
    };

    auto renderer = Renderer::create(renderer_config);
    if (!renderer) {
      std::println(stderr, "Failed to create renderer: {}", renderer.error().message());
      return -1;
    }

    // NOLINTNEXTLINE(misc-const-correctness) -- updated by input handlers once wired up
    Camera camera{ k_default_camera_position };

    while (!renderer->should_close()) {
      renderer->poll_events();
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

}// namespace vkgsplat
