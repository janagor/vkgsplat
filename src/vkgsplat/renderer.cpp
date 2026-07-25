#include <vkgsplat/renderer.hpp>

#include <bit>
#include <expected>
#include <functional>
#include <memory>
#include <print>
#include <string>
#include <system_error>
#include <utility>

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_window/window.hpp>

#include "app_state.hpp"
#include "backend/vulkan/depth_buffer.hpp"
#include "backend/vulkan/device.hpp"
#include "backend/vulkan/gpu_allocator.hpp"
#include "backend/vulkan/graphics_pipeline.hpp"
#include "backend/vulkan/imgui_overlay.hpp"
#include "backend/vulkan/renderer.hpp"
#include "backend/vulkan/swapchain.hpp"
#include "gs/binning.hpp"
#include "gs/pipeline.hpp"
#include "gs/projection.hpp"
#include "gs/rasterization.hpp"
#include "gs/sorting.hpp"
#include "io/ply/load_splats.hpp"
#include "mesh_gpu.hpp"
#include "sphere_setup.hpp"
#include "vulkan_context.hpp"

namespace vkgsplat {

using namespace gs;

namespace {

  [[nodiscard]] auto next_power_of_2(u32 value) -> u32
  {
    if (value <= 1U) { return 1U; }
    return 1U << static_cast<unsigned>(std::bit_width(static_cast<unsigned>(value - 1U)));
  }

}// namespace

struct Renderer::Impl
{
  Init init{};
  RenderData render_data{};
};

Renderer::Renderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Renderer::Renderer(Renderer &&) noexcept = default;
auto Renderer::operator=(Renderer &&) noexcept -> Renderer & = default;

Renderer::~Renderer()
{
  if (impl_ == nullptr) { return; }
  cleanup(impl_->init, impl_->render_data);
}

auto Renderer::create(RendererConfig const &config, Window &window) -> std::expected<Renderer, Error>
{
  auto impl = std::make_unique<Impl>();
  impl->init.window = &window;

  auto loaded = load_splats_from_ply(config.ply_path, config.splat_count);
  if (!loaded) { return std::unexpected(loaded.error()); }
  SplatCpuData const splats = std::move(*loaded);
  std::println("Loaded {} splats from {}", splats.geometries.size(), config.ply_path);

  auto const init_result = device_initialization(impl->init, config.enable_validation);
  if (!init_result.has_value()) { return std::unexpected(init_result.error()); }

  auto gpu_allocator =
    vulkan::GPUAllocator::create(impl->init.instance, impl->init.device, impl->init.device.physical_device);
  if (!gpu_allocator) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to create GPU allocator"));
  }
  impl->init.gpu_allocator = std::move(*gpu_allocator);

  auto swapchain = vulkan::Swapchain::create(impl->init.device, window.framebuffer_extent(), std::ref(impl->init.disp));
  if (!swapchain) { return std::unexpected(make_error(std::errc::invalid_argument, "Failed to create swapchain")); }
  impl->init.swapchain = std::make_unique<vulkan::Swapchain>(std::move(*swapchain));

  if (auto queues = get_queues(impl->init, impl->render_data); !queues) { return std::unexpected(queues.error()); }

  if (!create_sphere_buffers(impl->init, impl->render_data, splats)) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to create PLY sphere buffers"));
  }

  impl->render_data.sort_size = next_power_of_2(impl->render_data.splat_count);
  if (!init_sphere_setup(impl->init, impl->render_data)) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize sphere setup"));
  }
  if (!init_binning(impl->init, impl->render_data)) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize gaussian binning"));
  }
  if (!init_projection(impl->init, impl->render_data)) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize gaussian projection"));
  }
  if (!init_sorting(impl->init, impl->render_data)) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize gaussian sorting"));
  }
  if (!init_rasterization(impl->init, impl->render_data)) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to initialize gaussian rasterization"));
  }
  record_gs_pipeline(impl->render_data);
  if (0 != create_graphics_pipeline(impl->init, impl->render_data)) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to create graphics pipeline"));
  }
  if (!create_depth_buffer(impl->init, impl->render_data)) {
    return std::unexpected(make_error(std::errc::invalid_argument, "Failed to create depth buffer"));
  }
  if (auto command_resources = create_command_resources(impl->init, impl->render_data); !command_resources) {
    return std::unexpected(command_resources.error());
  }
  if (auto sync_objects = create_sync_objects(impl->init, impl->render_data); !sync_objects) {
    return std::unexpected(sync_objects.error());
  }
  if (config.enable_imgui) {
    if (auto imgui = init_imgui_overlay(impl->init, impl->render_data); !imgui) {
      return std::unexpected(imgui.error());
    }
  }

  return Renderer{ std::move(impl) };
}

auto Renderer::draw(Camera const &camera) -> std::expected<void, Error>
{
  if (auto drawn = draw_frame(impl_->init, impl_->render_data, camera); !drawn) {
    return std::unexpected(drawn.error());
  }
  return {};
}

void Renderer::wait_idle() const { impl_->init.disp.deviceWaitIdle(); }

}// namespace vkgsplat
