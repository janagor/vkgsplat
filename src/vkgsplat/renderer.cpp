#include <vkgsplat/renderer.hpp>

#include <expected>
#include <memory>
#include <print>
#include <string>
#include <system_error>
#include <utility>

#include <beman/indirect/indirect.hpp>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/driver.hpp>
#include <vkgsplat/engine.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/utils.hpp>

#include "backend/vulkan/app_state.hpp"
#include "backend/vulkan/depth_buffer.hpp"
#include "backend/vulkan/graphics_pipeline.hpp"
#include "backend/vulkan/imgui_overlay.hpp"
#include "backend/vulkan/mesh_gpu.hpp"
#include "backend/vulkan/renderer.hpp"
#include "backend/vulkan/sphere_setup.hpp"
#include "backend/vulkan/vulkan_context.hpp"
#include "backend/vulkan/vulkan_driver.hpp"
#include "backend/vulkan/gs/binning.hpp"
#include "backend/vulkan/gs/pipeline.hpp"
#include "backend/vulkan/gs/projection.hpp"
#include "backend/vulkan/gs/rasterization.hpp"
#include "backend/vulkan/gs/sorting.hpp"
#include "io/ply/load_splats.hpp"

namespace vkgsplat {

namespace {

  [[nodiscard]] auto BuildRendererResources(Init &init, RenderData &render_data, RendererConfig const &config)
    -> std::expected<void, Error>
  {
    auto loaded = load_splats_from_ply(config.ply_path, config.splat_count);
    if (!loaded) { return std::unexpected(loaded.error()); }
    SplatCpuData const splats = std::move(*loaded);
    std::println("Loaded {} splats from {}", splats.geometries.size(), config.ply_path);
    {
      auto const &first_appearance = splats.appearances.at(0);
      std::println("appearance[0] f_dc=({}, {}, {})",
        first_appearance.f_dc.at(0),
        first_appearance.f_dc.at(1),
        first_appearance.f_dc.at(2));
    }

    if (auto queues = get_queues(init, render_data); !queues) { return std::unexpected(queues.error()); }

    if (!create_sphere_buffers(init, render_data, splats)) {
      return std::unexpected(make_error(std::errc::invalid_argument, "Failed to create PLY sphere buffers"));
    }

    render_data.sort_size = next_power_of_2(render_data.splat_count);
    if (!init_sphere_setup(init, render_data)) {
      return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize sphere setup"));
    }
    if (!gs::init_binning(init, render_data)) {
      return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize gaussian binning"));
    }
    if (!gs::init_projection(init, render_data)) {
      return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize gaussian projection"));
    }
    if (!gs::init_sorting(init, render_data)) {
      return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize gaussian sorting"));
    }
    if (!gs::init_rasterization(init, render_data)) {
      return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize gaussian rasterization"));
    }
    gs::record_gs_pipeline(render_data);
    if (0 != create_graphics_pipeline(init, render_data)) {
      return std::unexpected(make_error(std::errc::invalid_argument, "Failed to create graphics pipeline"));
    }
    if (!create_depth_buffer(init, render_data)) {
      return std::unexpected(make_error(std::errc::invalid_argument, "Failed to create depth buffer"));
    }
    if (auto command_resources = create_command_resources(init, render_data); !command_resources) {
      return std::unexpected(command_resources.error());
    }
    if (auto sync_objects = create_sync_objects(init, render_data); !sync_objects) {
      return std::unexpected(sync_objects.error());
    }
    if (config.enable_gpu_timers) {
      if (!render_data.gpu_pass_timer.create(init)) {
        return std::unexpected(
          make_error(std::errc::invalid_argument, "Failed to create GPU pass timestamp query pool"));
      }
    }
    if (config.enable_imgui) {
      if (auto imgui = init_imgui_overlay(init, render_data); !imgui) { return std::unexpected(imgui.error()); }
    }

    return {};
  }

  [[nodiscard]] auto AsVulkanDriver(Driver &driver) -> vulkan::VulkanDriver &
  {
    // Only VulkanDriver is registered today; multi-API will replace this with a safer registry.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    return static_cast<vulkan::VulkanDriver &>(driver);
  }

}// namespace

struct Renderer::Impl
{
  std::unique_ptr<Engine> owned_engine;
  Engine *engine{};
  RenderData render_data{};
};

Renderer::Renderer(beman::indirect::indirect<Impl> impl) : impl_(std::move(impl)) {}

Renderer::Renderer(Renderer &&) noexcept = default;
auto Renderer::operator=(Renderer &&) noexcept -> Renderer & = default;

Renderer::~Renderer() noexcept
{
  if (impl_.valueless_after_move() || impl_->engine == nullptr) { return; }
  cleanup(AsVulkanDriver(impl_->engine->driver()).init(), impl_->render_data);
}

auto Renderer::create(RendererConfig const &config, Platform &platform) -> std::expected<Renderer, Error>
{
  auto engine = Engine::create(EngineConfig{ .enable_validation = config.enable_validation }, platform);
  if (!engine) { return std::unexpected(engine.error()); }

  beman::indirect::indirect<Impl> impl;
  impl->owned_engine = std::make_unique<Engine>(std::move(*engine));
  impl->engine = impl->owned_engine.get();

  if (auto built = BuildRendererResources(AsVulkanDriver(impl->engine->driver()).init(), impl->render_data, config);
      !built) {
    return std::unexpected(built.error());
  }

  return Renderer{ std::move(impl) };
}

auto Renderer::create(RendererConfig const &config, Engine &engine) -> std::expected<Renderer, Error>
{
  beman::indirect::indirect<Impl> impl;
  impl->engine = &engine;

  if (auto built = BuildRendererResources(AsVulkanDriver(engine.driver()).init(), impl->render_data, config); !built) {
    return std::unexpected(built.error());
  }

  return Renderer{ std::move(impl) };
}

auto Renderer::draw(Camera const &camera) -> std::expected<void, Error>
{
  if (auto drawn = draw_frame(AsVulkanDriver(impl_->engine->driver()).init(), impl_->render_data, camera); !drawn) {
    return std::unexpected(drawn.error());
  }
  return {};
}

void Renderer::wait_idle() const noexcept { impl_->engine->wait_idle(); }

auto Engine::create_renderer(RendererConfig const &config) -> std::expected<Renderer, Error>
{ return Renderer::create(config, *this); }

}// namespace vkgsplat
