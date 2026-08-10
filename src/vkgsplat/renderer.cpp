#include <vkgsplat/renderer.hpp>

#include <expected>
#include <memory>
#include <print>
#include <string_view>
#include <system_error>
#include <utility>

#include <beman/indirect/indirect.hpp>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/driver.hpp>
#include <vkgsplat/engine.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_utility/utils.hpp>

#include "backend/vulkan/app_state.hpp"
#include "backend/vulkan/graphics_pipeline.hpp"
#include "backend/vulkan/gs/binning.hpp"
#include "backend/vulkan/gs/pipeline.hpp"
#include "backend/vulkan/gs/projection.hpp"
#include "backend/vulkan/gs/rasterization.hpp"
#include "backend/vulkan/gs/sorting.hpp"
#include "backend/vulkan/imgui_overlay.hpp"
#include "backend/vulkan/mesh_gpu.hpp"
#include "backend/vulkan/present_pacer.hpp"
#include "backend/vulkan/render_thread.hpp"
#include "backend/vulkan/renderer.hpp"
#include "backend/vulkan/screenshot.hpp"
#include "backend/vulkan/sphere_setup.hpp"
#include "backend/vulkan/vulkan_context.hpp"
#include "backend/vulkan/vulkan_driver.hpp"
#include "io/ply/load_splats.hpp"

namespace vkgsplat {

namespace {

  [[nodiscard]] auto BuildRendererResources(vulkan::Context &context, RenderData &render_data, RendererConfig const &config)
    -> std::expected<void, Error>
  {
    auto loaded = LoadSplatsFromPly(config.ply_path);
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

    if (auto queues = GetQueues(context, render_data); !queues) { return std::unexpected(queues.error()); }

    if (!CreateSphereBuffers(context, render_data, splats)) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "Failed to create PLY sphere buffers"));
    }

    render_data.lfd_grid = config.lfd_grid;
    render_data.view_cone_deg = config.view_cone_deg;
    if (render_data.lfd_grid.at(0) == 0U || render_data.lfd_grid.at(1) == 0U) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "lfd_grid columns and rows must be >= 1"));
    }

    render_data.sort_size = NextPowerOf2(render_data.splat_count);
    if (!InitSphereSetup(context, render_data)) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "Failed to initialize sphere setup"));
    }
    if (!gs::InitBinning(context, render_data)) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "Failed to initialize gaussian binning"));
    }
    if (!gs::InitProjection(context, render_data)) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "Failed to initialize gaussian projection"));
    }
    if (!gs::InitSorting(context, render_data)) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "Failed to initialize gaussian sorting"));
    }
    if (!gs::InitRasterization(context, render_data)) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "Failed to initialize gaussian rasterization"));
    }
    gs::RecordGsPipeline(render_data);
    if (0 != CreateGraphicsPipeline(context, render_data)) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "Failed to create graphics pipeline"));
    }
    if (auto command_resources = CreateCommandResources(context, render_data); !command_resources) {
      return std::unexpected(command_resources.error());
    }
    if (auto sync_objects = CreateSyncObjects(context, render_data); !sync_objects) {
      return std::unexpected(sync_objects.error());
    }
    if (config.enable_gpu_timers) {
      if (!render_data.gpu_pass_timer.create(context)) {
        return std::unexpected(
          MakeError(std::errc::invalid_argument, "Failed to create GPU pass timestamp query pool"));
      }
    }
    if (config.enable_imgui) {
      if (auto imgui = InitImguiOverlay(context, render_data); !imgui) { return std::unexpected(imgui.error()); }
    }

    render_data.present_pacer = PresentPacer::TryCreate(context, config.frame_rate);
    if (config.frame_rate.IsPacingRequested() && render_data.present_pacer == nullptr
        && !context.present_timing_enabled) {
      // Warning already printed by TryCreate / device context.
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
  std::unique_ptr<RenderThread> render_thread;
};

Renderer::Renderer(beman::indirect::indirect<Impl> impl) : impl_(std::move(impl)) {}

Renderer::Renderer(Renderer &&) noexcept = default;
auto Renderer::operator=(Renderer &&) noexcept -> Renderer & = default;

Renderer::~Renderer() noexcept
{
  if (impl_.valueless_after_move() || impl_->engine == nullptr) { return; }
  impl_->render_thread->Stop();
  Cleanup(AsVulkanDriver(impl_->engine->driver()).context(), impl_->render_data);
}

auto Renderer::create(RendererConfig const &config, Platform &platform) -> std::expected<Renderer, Error>
{
  auto engine = Engine::create(
    EngineConfig{ .enable_validation = config.enable_validation,
      .request_present_timing = config.frame_rate.IsPacingRequested() },
    platform);
  if (!engine) { return std::unexpected(engine.error()); }

  beman::indirect::indirect<Impl> impl;
  impl->owned_engine = std::make_unique<Engine>(std::move(*engine));
  impl->engine = impl->owned_engine.get();

  if (auto built = BuildRendererResources(AsVulkanDriver(impl->engine->driver()).context(), impl->render_data, config);
    !built) {
    return std::unexpected(built.error());
  }

  impl->render_thread = std::make_unique<RenderThread>();
  impl->render_thread->Start(&AsVulkanDriver(impl->engine->driver()).context(), &impl->render_data);

  return Renderer{ std::move(impl) };
}

auto Renderer::create(RendererConfig const &config, Engine &engine) -> std::expected<Renderer, Error>
{
  beman::indirect::indirect<Impl> impl;
  impl->engine = &engine;

  if (auto built = BuildRendererResources(AsVulkanDriver(engine.driver()).context(), impl->render_data, config); !built) {
    return std::unexpected(built.error());
  }

  impl->render_thread = std::make_unique<RenderThread>();
  impl->render_thread->Start(&AsVulkanDriver(engine.driver()).context(), &impl->render_data);

  return Renderer{ std::move(impl) };
}

auto Renderer::draw(Camera const &camera) -> std::expected<void, Error>
{
  auto const &context = AsVulkanDriver(impl_->engine->driver()).context();
  auto const aspect_ratio =
    static_cast<f64>(context.swapchain->extent().width) / static_cast<f64>(context.swapchain->extent().height);
  return impl_->render_thread->SubmitFrame(camera, aspect_ratio);
}

auto Renderer::save_frame_png(std::string_view path) -> std::expected<void, Error>
{
  wait_idle();
  auto &context = AsVulkanDriver(impl_->engine->driver()).context();
  return vulkan::SaveColorTargetPng(context, impl_->render_data, path);
}

void Renderer::wait_idle() noexcept { impl_->render_thread->WaitIdle(); }

auto Engine::create_renderer(RendererConfig const &config) -> std::expected<Renderer, Error>
{ return Renderer::create(config, *this); }

}// namespace vkgsplat
