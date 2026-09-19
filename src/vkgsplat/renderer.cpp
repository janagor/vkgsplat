#include <vkgsplat/renderer.hpp>

#include <algorithm>
#include <array>
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
#include <vkgsplat/lfd_config.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_utility/utils.hpp>

#include "vulkan/app_state.hpp"
#include "vulkan/graphics_pipeline.hpp"
#include "vulkan/gs/binning.hpp"
#include "vulkan/gs/pipeline.hpp"
#include "vulkan/gs/projection.hpp"
#include "vulkan/gs/rasterization.hpp"
#include "vulkan/gs/sorting.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "vulkan/mesh_gpu.hpp"
#include "vulkan/present_pacer.hpp"
#include "vulkan/render_thread.hpp"
#include "vulkan/renderer.hpp"
#include "vulkan/screenshot.hpp"
#include "vulkan/sphere_setup.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_driver.hpp"
#include <vkgsplat_io/load_splats.hpp>
#include <vkgsplat_io/splat_cpu.hpp>

namespace vkgsplat {

namespace {

  [[nodiscard]] auto BuildRendererResources(vulkan::Context &context,
    RenderData &render_data,
    RendererConfig const &config) -> std::expected<void, Error>
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
    render_data.lfd_view_order = config.lfd_view_order;
    if (render_data.lfd_grid.at(0) == 0U || render_data.lfd_grid.at(1) == 0U) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "lfd_grid columns and rows must be >= 1"));
    }
    if (render_data.lfd_view_order.empty()) {
      render_data.lfd_view_order = BuildLfdViewOrder(LfdViewLayout::kNormal, render_data.lfd_grid);
    }
    if (auto const validated = ValidateLfdViewOrder(render_data.lfd_view_order, render_data.lfd_grid); !validated) {
      return std::unexpected(validated.error());
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
  if (impl_.valueless_after_move()) { return; }
  if (impl_->render_thread != nullptr) { impl_->render_thread->Stop(); }
  if (impl_->engine != nullptr) { Cleanup(AsVulkanDriver(impl_->engine->driver()).context(), impl_->render_data); }
}

auto Renderer::create(RendererConfig const &config, Platform &platform) -> std::expected<Renderer, Error>
{
  auto engine = Engine::create(EngineConfig{ .enable_validation = config.enable_validation,
                                 .request_present_timing = config.frame_rate.IsPacingRequested() },
    platform);
  if (!engine) { return std::unexpected(engine.error()); }

  beman::indirect::indirect<Impl> impl;
  impl->owned_engine = std::make_unique<Engine>(std::move(*engine));
  impl->engine = impl->owned_engine.get();

  auto &context = AsVulkanDriver(impl->engine->driver()).context();
  if (auto built = BuildRendererResources(context, impl->render_data, config); !built) {
    Cleanup(context, impl->render_data);
    return std::unexpected(built.error());
  }

  impl->render_thread = std::make_unique<RenderThread>();
  impl->render_thread->Start(&context, &impl->render_data);

  return Renderer{ std::move(impl) };
}

auto Renderer::create(RendererConfig const &config, Engine &engine) -> std::expected<Renderer, Error>
{
  beman::indirect::indirect<Impl> impl;
  impl->engine = &engine;

  auto &context = AsVulkanDriver(engine.driver()).context();
  if (auto built = BuildRendererResources(context, impl->render_data, config); !built) {
    Cleanup(context, impl->render_data);
    return std::unexpected(built.error());
  }

  impl->render_thread = std::make_unique<RenderThread>();
  impl->render_thread->Start(&context, &impl->render_data);

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

void Renderer::set_lfd_emulate(bool active)
{
  wait_idle();
  impl_->render_data.lfd_emulate_active = active;
  if (active) { impl_->render_data.lfd_emulate_cell = { 0U, 0U }; }

  auto &context = AsVulkanDriver(impl_->engine->driver()).context();
  static_cast<void>(gs::RecreateRasterizationColorTarget(context, impl_->render_data));
}

auto Renderer::lfd_emulate_active() const noexcept -> bool { return impl_->render_data.lfd_emulate_active; }

void Renderer::set_lfd_emulate_cell(u32 col, u32 row)
{
  auto const cols = std::max(1U, impl_->render_data.lfd_grid.at(0));
  auto const rows = std::max(1U, impl_->render_data.lfd_grid.at(1));
  impl_->render_data.lfd_emulate_cell = { std::min(col, cols - 1U), std::min(row, rows - 1U) };
}

auto Renderer::lfd_emulate_cell() const noexcept -> std::array<u32, 2> { return impl_->render_data.lfd_emulate_cell; }

auto Renderer::lfd_grid() const noexcept -> std::array<u32, 2> { return impl_->render_data.lfd_grid; }

auto Engine::create_renderer(RendererConfig const &config) -> std::expected<Renderer, Error>
{ return Renderer::create(config, *this); }

}// namespace vkgsplat
