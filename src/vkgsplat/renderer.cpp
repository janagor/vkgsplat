#include <vkgsplat/renderer.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <VkBootstrap.h>
#include <beman/indirect/indirect.hpp>
#include <vulkan/vulkan_core.h>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/driver.hpp>
#include <vkgsplat/engine.hpp>
#include <vkgsplat/frame.hpp>
#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/lfd_config.hpp>
#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_utility/utils.hpp>
#include <vkgsplat_video/encoded_packet.hpp>

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
#include "vulkan/sphere_setup.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_driver.hpp"
#include "vulkan/video/color_converter.hpp"
#include "vulkan/video/video_encoder.hpp"
#include <vkgsplat_io/load_splats.hpp>
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_video/recording_session.hpp>

namespace vkgsplat {

namespace {

  [[nodiscard]] auto IsValidQuiltSize(RenderData const &data) -> bool
  {
    if (data.quilt_size == std::array<u32, 2>{ 0U, 0U }) { return true; }
    if (data.lfd_grid == std::array<u32, 2>{ 1U, 1U }) { return false; }
    if (data.quilt_size.at(0) == 0U || data.quilt_size.at(1) == 0U) { return false; }
    return data.quilt_size.at(0) % data.lfd_grid.at(0) == 0U && data.quilt_size.at(1) % data.lfd_grid.at(1) == 0U;
  }

  [[nodiscard]] auto CreateGpuPassTimer(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
  {
    u64 const view_count_wide = static_cast<u64>(data.lfd_grid.at(0)) * data.lfd_grid.at(1);
    if (view_count_wide > std::numeric_limits<u32>::max()) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "lfd_grid contains too many views"));
    }
    auto const queue_index = context.device.get_queue_index(vkb::QueueType::graphics);
    auto const families = context.device.physical_device.get_queue_families();
    if (!queue_index || queue_index.value() >= families.size()
        || families.at(queue_index.value()).timestampValidBits == 0U) {
      return std::unexpected{ MakeError(
        std::errc::function_not_supported, "graphics queue does not support GPU timestamp queries") };
    }
    if (!data.gpu_pass_timer.create(context, static_cast<u32>(view_count_wide))) {
      return std::unexpected(MakeError(std::errc::invalid_argument,
        "Failed to create GPU pass timestamp query pool (graphics queue timestamps must be supported)"));
    }
    return {};
  }

  [[nodiscard]] auto ValidateGpuTimingConfig(RendererConfig const &config) -> std::expected<void, Error>
  {
    if (config.capture_gpu_timing_samples && !config.enable_gpu_timers) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "GPU timing collection requires GPU timers") };
    }
    return {};
  }

  [[nodiscard]] auto ValidateOffscreenConfig(RendererConfig const &config, Platform const &platform)
    -> std::expected<void, Error>
  {
    if (platform.surface_mode() != SurfaceMode::kOffscreen) { return {}; }
    auto const extent = platform.framebuffer_extent();
    if (extent.width == 0U || extent.height == 0U) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "offscreen extent must be positive") };
    }
    if (config.enable_imgui || config.frame_rate.IsPacingRequested()
        || config.submission_policy != FrameSubmissionPolicy::kLossless) {
      return std::unexpected{ MakeError(std::errc::invalid_argument,
        "offscreen rendering requires disabled ImGui and pacing, and lossless submission") };
    }
    return {};
  }

  [[nodiscard]] auto ValidateOutputConfig(RendererConfig const &config, vulkan::Context const &context)
    -> std::expected<void, Error>
  {
    VkExtent2D const extent = context.swapchain == nullptr
                                ? VkExtent2D{ .width = context.platform->framebuffer_extent().width,
                                    .height = context.platform->framebuffer_extent().height }
                                : context.swapchain->vk_extent();
    if (extent.width == 0U || extent.height == 0U) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "render extent must be positive") };
    }
    if (auto valid = ValidateOffscreenConfig(config, *context.platform); !valid) { return valid; }
    return {};
  }

  [[nodiscard]] auto ConfigureLfd(RenderData &render_data, RendererConfig const &config) -> std::expected<void, Error>
  {
    render_data.lfd_grid = config.lfd_grid;
    render_data.quilt_size = config.quilt_size;
    render_data.view_cone_deg = config.view_cone_deg;
    render_data.lfd_view_order = config.lfd_view_order;
    if (render_data.lfd_grid.at(0) == 0U || render_data.lfd_grid.at(1) == 0U) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "lfd_grid columns and rows must be >= 1"));
    }
    if (!IsValidQuiltSize(render_data)) {
      return std::unexpected(MakeError(std::errc::invalid_argument,
        "quilt_size requires a multiview lfd_grid and positive atlas dimensions divisible by that grid"));
    }
    if (render_data.lfd_view_order.empty()) {
      render_data.lfd_view_order = BuildLfdViewOrder(LfdViewLayout::kNormal, render_data.lfd_grid);
    }
    if (auto const validated = ValidateLfdViewOrder(render_data.lfd_view_order, render_data.lfd_grid); !validated) {
      return std::unexpected(validated.error());
    }
    return {};
  }

  [[nodiscard]] auto BuildRendererResources(vulkan::Context &context,
    RenderData &render_data,
    RendererConfig const &config) -> std::expected<void, Error>
  {
    if (auto valid = ValidateOutputConfig(config, context); !valid) { return valid; }
    auto const output_extent =
      context.swapchain == nullptr ? context.platform->framebuffer_extent() : context.swapchain->extent();
    render_data.output_extent = { .width = output_extent.width, .height = output_extent.height };
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

    if (auto configured = ConfigureLfd(render_data, config); !configured) {
      return std::unexpected(configured.error());
    }

    if (!CreateSphereBuffers(context, render_data, splats)) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "Failed to create PLY sphere buffers"));
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
      if (auto timer = CreateGpuPassTimer(context, render_data); !timer) { return std::unexpected(timer.error()); }
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

namespace {

  class VulkanVideoRecordingSession final : public video::RecordingSession
  {
  public:
    VulkanVideoRecordingSession(RenderThread &worker, VkExtent2D extent,
      std::unique_ptr<vulkan::video::GpuColorConverter> converter,
      std::unique_ptr<vulkan::video::VideoEncoder> encoder) noexcept
      : worker_{ &worker }, extent_{ extent }, converter_{ std::move(converter) }, encoder_{ std::move(encoder) }
    {}

    [[nodiscard]] auto CaptureFrame(Camera const &camera, u64 frame_index)
      -> std::expected<EncodedPacket, Error> override
    {
      if (finished_ || failed_) {
        return std::unexpected(MakeError(std::errc::operation_not_permitted,
          "video recording session is finished or failed"));
      }
      auto const aspect_ratio = static_cast<f64>(extent_.width) / static_cast<f64>(extent_.height);
      auto packet = worker_->CaptureVideoFrame(camera, aspect_ratio, *converter_, *encoder_, frame_index);
      if (!packet) { failed_ = true; }
      return packet;
    }

    [[nodiscard]] auto Finish() -> std::expected<void, Error> override
    {
      if (finished_ || failed_) {
        return std::unexpected(MakeError(std::errc::operation_not_permitted,
          "video recording session is finished or failed"));
      }
      auto finished = encoder_->Finish();
      if (!finished) {
        failed_ = true;
        return finished;
      }
      finished_ = true;
      return {};
    }

    [[nodiscard]] auto ParameterSets() const noexcept -> std::span<std::byte const> override
    { return encoder_->ParameterSets(); }

  private:
    RenderThread *worker_;
    VkExtent2D extent_{};
    std::unique_ptr<vulkan::video::GpuColorConverter> converter_;
    std::unique_ptr<vulkan::video::VideoEncoder> encoder_;
    bool finished_ = false;
    bool failed_ = false;
  };

}// namespace

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
  if (auto validated = ValidateGpuTimingConfig(config); !validated) { return std::unexpected{ validated.error() }; }
  if (auto validated = ValidateOffscreenConfig(config, platform); !validated) {
    return std::unexpected{ validated.error() };
  }
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
  impl->render_thread->Start(&context, &impl->render_data, config.submission_policy, config.capture_gpu_timing_samples);

  return Renderer{ std::move(impl) };
}

auto Renderer::create(RendererConfig const &config, Engine &engine) -> std::expected<Renderer, Error>
{
  if (auto validated = ValidateGpuTimingConfig(config); !validated) { return std::unexpected{ validated.error() }; }
  if (auto validated = ValidateOffscreenConfig(config, engine.platform()); !validated) {
    return std::unexpected{ validated.error() };
  }
  beman::indirect::indirect<Impl> impl;
  impl->engine = &engine;

  auto &context = AsVulkanDriver(engine.driver()).context();
  if (auto built = BuildRendererResources(context, impl->render_data, config); !built) {
    Cleanup(context, impl->render_data);
    return std::unexpected(built.error());
  }

  impl->render_thread = std::make_unique<RenderThread>();
  impl->render_thread->Start(&context, &impl->render_data, config.submission_policy, config.capture_gpu_timing_samples);

  return Renderer{ std::move(impl) };
}

auto Renderer::draw(Camera const &camera) -> std::expected<void, Error>
{
  auto const extent = impl_->engine->platform().framebuffer_extent();
  auto const aspect_ratio =
    extent.height > 0U ? static_cast<f64>(extent.width) / static_cast<f64>(extent.height) : camera.aspect_ratio();
  return impl_->render_thread->SubmitFrame(camera, aspect_ratio);
}

auto Renderer::render_frame(Camera const &camera) -> std::expected<RgbaFrame, Error>
{
  if (impl_->engine->platform().surface_mode() != SurfaceMode::kOffscreen) {
    return std::unexpected{ MakeError(std::errc::operation_not_supported, "render_frame requires offscreen mode") };
  }
  auto const extent = impl_->engine->platform().framebuffer_extent();
  f64 const aspect_ratio = static_cast<f64>(extent.width) / static_cast<f64>(extent.height);
  return impl_->render_thread->CaptureFrame(camera, aspect_ratio);
}

auto Renderer::create_video_recording_session(u32 width, u32 height,
  u32 fps_numerator, u32 fps_denominator)
  -> std::expected<std::unique_ptr<video::RecordingSession>, Error>
{
  auto &context = AsVulkanDriver(impl_->engine->driver()).context();
  if (!context.video_encode) {
    return std::unexpected(MakeError(std::errc::function_not_supported,
      "renderer engine was not created with H.264 video encoding enabled"));
  }
  if (context.swapchain != nullptr || width == 0U || height == 0U || width != impl_->render_data.color_width
      || height != impl_->render_data.color_height) {
    return std::unexpected(MakeError(std::errc::invalid_argument,
      "video recording requires an offscreen renderer with matching dimensions"));
  }
  VkExtent2D const extent{ .width = width, .height = height };
  auto device = *context.video_encode;
  auto selection = device.capabilities.select({
    .require_conversion = true,
    .require_references = false,
    .required_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
    .require_transfer_destination = true,
    .required_extent = extent,
  });
  if (!selection) {
    return std::unexpected(MakeError(std::errc::function_not_supported,
      "no NV12 H.264 encoding profile supports this recording extent"));
  }
  device.selection = *selection;
  auto converter = vulkan::video::GpuColorConverter::Create(context, device, extent);
  if (!converter) { return std::unexpected(converter.error()); }
  auto encoder = vulkan::video::VideoEncoder::Create(context, std::move(device), extent,
    fps_numerator, fps_denominator);
  if (!encoder) { return std::unexpected(encoder.error()); }
  return std::unique_ptr<video::RecordingSession>{
    std::make_unique<VulkanVideoRecordingSession>(*impl_->render_thread,
      extent, std::move(*converter), std::move(*encoder)) };
}

auto Renderer::poll_error() const -> std::optional<Error> { return impl_->render_thread->PollError(); }

auto Renderer::completed_frame_count() const -> u64 { return impl_->render_thread->CompletedFrameCount(); }

auto Renderer::recreated_frame_count() const -> u64 { return impl_->render_thread->RecreatedFrameCount(); }

auto Renderer::device_info() const -> RenderDeviceInfo
{
  auto const &properties = AsVulkanDriver(impl_->engine->driver()).context().device.physical_device.properties;
  auto const device_name = std::span{ properties.deviceName };
  return RenderDeviceInfo{
    .name = std::string{ device_name.begin(), std::ranges::find(device_name, '\0') },
    .vendor_id = properties.vendorID,
    .device_id = properties.deviceID,
    .driver_version = properties.driverVersion,
    .vulkan_api_version = properties.apiVersion,
  };
}

auto Renderer::capture_latest_frame() -> std::expected<RgbaFrame, Error>
{ return impl_->render_thread->ReadLatestFrame(); }

auto Renderer::wait_idle() -> std::expected<void, Error> { return impl_->render_thread->WaitIdle(); }

auto Renderer::take_gpu_timings() -> std::expected<std::vector<GpuFrameTimings>, Error>
{ return impl_->render_thread->TakeGpuTimings(); }

auto Renderer::reserve_gpu_timing_samples(size_t count) -> std::expected<void, Error>
{ return impl_->render_thread->ReserveGpuTimingSamples(count); }

auto Renderer::gpu_timestamp_period_ns() const -> double
{
  auto const &context = AsVulkanDriver(impl_->engine->driver()).context();
  return static_cast<double>(context.device.physical_device.properties.limits.timestampPeriod);
}

auto Renderer::gpu_timestamp_valid_bits() const -> u32
{
  auto const &context = AsVulkanDriver(impl_->engine->driver()).context();
  auto const queue_index = context.device.get_queue_index(vkb::QueueType::graphics);
  if (!queue_index) { return 0U; }
  auto const families = context.device.physical_device.get_queue_families();
  if (queue_index.value() >= families.size()) { return 0U; }
  return families.at(queue_index.value()).timestampValidBits;
}

auto Renderer::set_lfd_emulate(bool active) -> std::expected<void, Error>
{
  auto *const worker = impl_->render_thread.get();
  auto *const data = &impl_->render_data;
  auto *const context = &AsVulkanDriver(impl_->engine->driver()).context();
  return worker->RunExclusive([worker, data, context, active]() -> std::expected<void, Error> {
    worker->InvalidateColorTarget();
    data->lfd_emulate_active = active;
    if (active) { data->lfd_emulate_cell = { 0U, 0U }; }
    if (!gs::RecreateRasterizationColorTarget(*context, *data)) {
      return std::unexpected{ MakeError(std::errc::io_error, "failed to recreate rasterization color target") };
    }
    return {};
  });
}

auto Renderer::lfd_emulate_active() const noexcept -> bool { return impl_->render_thread->LfdEmulateActive(); }

auto Renderer::set_lfd_emulate_cell(u32 col, u32 row) -> std::expected<void, Error>
{
  auto *const data = &impl_->render_data;
  return impl_->render_thread->RunExclusive([data, col, row]() -> std::expected<void, Error> {
    auto const cols = std::max(1U, data->lfd_grid.at(0));
    auto const rows = std::max(1U, data->lfd_grid.at(1));
    data->lfd_emulate_cell = { std::min(col, cols - 1U), std::min(row, rows - 1U) };
    return {};
  });
}

auto Renderer::lfd_emulate_cell() const noexcept -> std::array<u32, 2>
{ return impl_->render_thread->LfdEmulateCell(); }

auto Renderer::lfd_grid() const noexcept -> std::array<u32, 2> { return impl_->render_data.lfd_grid; }

auto Engine::create_renderer(RendererConfig const &config) -> std::expected<Renderer, Error>
{ return Renderer::create(config, *this); }

}// namespace vkgsplat
