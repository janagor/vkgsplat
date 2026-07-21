#include <vkgsplat/renderer.hpp>

#include <bit>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <system_error>
#include <utility>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/error.hpp>
#include <vkgsplat/types.hpp>

#include "app_state.hpp"
#include "backend/vulkan/depth_buffer.hpp"
#include "backend/vulkan/device.hpp"
#include "backend/vulkan/gpu_allocator.hpp"
#include "backend/vulkan/graphics_pipeline.hpp"
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

  [[nodiscard]] auto make_error(std::string message) -> Error
  { return Error{ std::make_error_code(std::errc::invalid_argument), std::move(message) }; }

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

auto Renderer::create(RendererConfig const &config) -> std::expected<Renderer, Error>
{
  auto impl = std::make_unique<Impl>();
  impl->render_data.procedural = config.source == SplatSource::Procedural;

  std::optional<SplatCpuData> splats{};
  if (!impl->render_data.procedural) {
    auto loaded = load_splats_from_ply(config.ply_path, config.splat_count);
    if (!loaded) { return std::unexpected(loaded.error()); }
    splats = std::move(*loaded);
    std::println("Loaded {} splats from {}", splats->geometries.size(), config.ply_path);
  } else {
    std::println("Using procedural mode with {} spheres", config.splat_count);
  }

  auto const init_result = device_initialization(impl->init);
  if (!init_result.has_value()) { return std::unexpected(init_result.error()); }

  auto gpu_allocator =
    vulkan::GPUAllocator::create(impl->init.instance, impl->init.device, impl->init.device.physical_device);
  if (!gpu_allocator) { return std::unexpected(make_error("Failed to create GPU allocator")); }
  impl->init.gpu_allocator = std::move(*gpu_allocator);

  auto swapchain = vulkan::Swapchain::create(impl->init.device, impl->init.window, std::ref(impl->init.disp));
  if (!swapchain) { return std::unexpected(make_error("Failed to create swapchain")); }
  impl->init.swapchain = std::make_unique<vulkan::Swapchain>(std::move(*swapchain));

  if (auto queues = get_queues(impl->init, impl->render_data); !queues) { return std::unexpected(queues.error()); }

  if (impl->render_data.procedural) {
    if (!create_sphere_buffers(impl->init, impl->render_data, config.splat_count)) {
      return std::unexpected(make_error("Failed to create procedural sphere buffers"));
    }
  } else {
    if (!create_sphere_buffers(impl->init, impl->render_data, config.splat_count, std::cref(*splats))) {
      return std::unexpected(make_error("Failed to create PLY sphere buffers"));
    }
  }

  impl->render_data.sort_size = next_power_of_2(impl->render_data.splat_count);
  if (!init_sphere_setup(impl->init, impl->render_data)) {
    return std::unexpected(make_error("Failed to initialize sphere setup"));
  }
  if (!init_binning(impl->init, impl->render_data)) {
    return std::unexpected(make_error("Failed to initialize gaussian binning"));
  }
  if (!init_projection(impl->init, impl->render_data)) {
    return std::unexpected(make_error("Failed to initialize gaussian projection"));
  }
  if (!init_sorting(impl->init, impl->render_data)) {
    return std::unexpected(make_error("Failed to initialize gaussian sorting"));
  }
  if (!init_rasterization(impl->init, impl->render_data)) {
    return std::unexpected(make_error("Failed to initialize gaussian rasterization"));
  }
  record_gs_pipeline(impl->render_data);
  if (0 != create_graphics_pipeline(impl->init, impl->render_data)) {
    return std::unexpected(make_error("Failed to create graphics pipeline"));
  }
  if (!create_depth_buffer(impl->init, impl->render_data)) {
    return std::unexpected(make_error("Failed to create depth buffer"));
  }
  if (auto command_resources = create_command_resources(impl->init, impl->render_data); !command_resources) {
    return std::unexpected(command_resources.error());
  }
  if (auto sync_objects = create_sync_objects(impl->init, impl->render_data); !sync_objects) {
    return std::unexpected(sync_objects.error());
  }

  return Renderer{ std::move(impl) };
}

void Renderer::poll_events() const
{
  (void)impl_;
  glfwPollEvents();
}

auto Renderer::should_close() const -> bool { return glfwWindowShouldClose(impl_->init.window) != 0; }

auto Renderer::draw(Camera const &camera) -> std::expected<void, Error>
{
  if (auto drawn = draw_frame(impl_->init, impl_->render_data, camera); !drawn) {
    return std::unexpected(drawn.error());
  }
  return {};
}

void Renderer::wait_idle() const { impl_->init.disp.deviceWaitIdle(); }

}// namespace vkgsplat
