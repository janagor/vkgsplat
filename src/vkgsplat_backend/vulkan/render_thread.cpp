#include "render_thread.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "app_state.hpp"
#include "gs/pipeline.hpp"
#include "video/color_converter.hpp"
#include "video/video_encoder.hpp"
#include "vkexec_error.hpp"
#include "vulkan/capture/readback.hpp"
#include "vulkan/command/command.hpp"
#include "vulkan/descriptor/descriptor_heap.hpp"
#include "vulkan/frame_context.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "vulkan/initializers.hpp"
#include "vulkan/renderer.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/camera.hpp>
#include <vkgsplat/frame.hpp>
#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_video/encoded_packet.hpp>

#include <vkexec/queue_submit.hpp>
#include <vkexec_extensions/timeline_semaphore/frame_ring.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace {


  [[nodiscard]] auto
    RecordSphereDraw(vulkan::Context &context, RenderData &data, VkCommandBuffer command_buffer, size_t image_index)
      -> std::expected<void, Error>
  {
    BindDescriptorHeap(context, data, command_buffer);
    if (auto recorded = gs::EvalGsPipeline(context, data, command_buffer); !recorded) { return recorded; }
    data.gpu_pass_timer.end_frame(context, data.current_slot, command_buffer);
    return RecordImguiOverlay(context, data, command_buffer, image_index);
  }

  [[nodiscard]] auto
    ResolveGpuPassTimings(vulkan::Context const &context, RenderData &data, size_t frame_slot, bool capture_samples)
      -> std::expected<std::optional<GpuFrameTimings>, Error>
  {
    if (!data.gpu_pass_timer.enabled()) { return std::nullopt; }
    auto resolved = data.gpu_pass_timer.resolve(context, frame_slot);
    if (!resolved || !resolved->has_value()) { return resolved; }
    if (data.imgui != nullptr) { UpdateImguiGpuTimings(data); }
    if (capture_samples || data.imgui != nullptr) { return resolved; }
    static auto last_print = std::chrono::steady_clock::time_point{};
    auto const now = std::chrono::steady_clock::now();
    if (last_print.time_since_epoch().count() == 0 || now - last_print >= std::chrono::seconds{ 1 }) {
      last_print = now;
      auto const &pass_ms = data.gpu_pass_timer.last_ms();
      std::println(
        "GPU measured: {:.2f} ms (proj {:.2f} view proj {:.2f} bin {:.2f} prep {:.2f} radix {:.2f} raster {:.2f})",
        data.gpu_pass_timer.total_ms(),
        pass_ms.at(static_cast<size_t>(GpuPass::kProjection)),
        pass_ms.at(static_cast<size_t>(GpuPass::kMultiviewProjection)),
        pass_ms.at(static_cast<size_t>(GpuPass::kBinning)),
        pass_ms.at(static_cast<size_t>(GpuPass::kPrepareSort)),
        pass_ms.at(static_cast<size_t>(GpuPass::kRadixSort)),
        pass_ms.at(static_cast<size_t>(GpuPass::kRasterize)));
    }
    return resolved;
  }

  [[nodiscard]] auto PresentSwapchainImage(vulkan::Context &context,
    RenderData &data,
    uint32_t image_index,
    VkSemaphore finished_semaphore) -> std::expected<VkResult, Error>
  {
    std::array<VkSwapchainKHR, 1> const swap_chains = { context.swapchain->handle() };
    std::array<VkSemaphore, 1> present_wait = { finished_semaphore };
    auto present_info = initializers::PresentInfoKHR(present_wait, swap_chains, std::span{ &image_index, 1 });

    if (data.present_pacer != nullptr) { data.present_pacer->PreparePresent(context, present_info); }
    auto const queue_lock = context.vkexec_context->lock_queue(data.present_queue);
    VkResult const result = context.disp.queuePresentKHR(data.present_queue, &present_info);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) { RecordImguiPresentedFrame(data); }
    if (data.present_pacer != nullptr) { data.present_pacer->AfterPresent(context); }
    return result;
  }

  [[nodiscard]] auto RetireAcquiredImage(vulkan::Context &context, RenderData &data, Error error)
    -> std::expected<void, Error>
  {
    if (error.code() == std::errc::no_such_device) { return std::unexpected{ std::move(error) }; }
    if (auto recreated = RecreateSwapchain(context, data); !recreated) { return std::unexpected{ recreated.error() }; }
    return std::unexpected{ std::move(error) };
  }

  [[nodiscard]] auto WaitFrameResources(vulkan::Context const &context,
    RenderData &data,
    vkexec::owned::frame_ring &ring,
    size_t frame_slot,
    bool capture_samples) -> std::expected<std::optional<GpuFrameTimings>, Error>
  {
    if (auto waited = ring.wait_slot(frame_slot); !waited) {
      return std::unexpected{ vulkan::VkexecFailure(waited.error()) };
    }
    // Gaussian buffers and the color target are shared across frame slots.
    size_t const previous_slot = (frame_slot + kFrameSlotCount - 1U) % kFrameSlotCount;
    if (auto waited = ring.wait_slot(previous_slot); !waited) {
      return std::unexpected{ vulkan::VkexecFailure(waited.error()) };
    }
    return ResolveGpuPassTimings(context, data, frame_slot, capture_samples);
  }

  [[nodiscard]] auto RecordAndSubmitFrame(vulkan::Context &context,
    RenderData &data,
    vkexec::owned::frame_ring &ring,
    size_t frame_slot,
    uint32_t image_index) -> std::expected<void, Error>
  {
    if (auto waited = ring.wait_image(image_index); !waited) {
      return std::unexpected{ vulkan::VkexecFailure(waited.error()) };
    }

    FrameSetup &setup = data.frames.at(frame_slot).setup;
    BuildFrameSetupGpu(data, setup, image_index);
    ApplyFrameSetup(data, frame_slot);

    auto *command_buffer = data.command_buffers.at(image_index).handle();
    auto recorded = vulkan::WithCommand(std::ref(context.disp),
      command_buffer,
      [&](vkb::DispatchTable &, VkCommandBuffer cmd) -> std::expected<void, Error> {
        return RecordSphereDraw(context, data, cmd, image_index);
      });
    if (!recorded) { return std::unexpected{ recorded.error() }; }

    auto const signal_value = ring.allocate_signal_value();
    auto submit_sync = ring.make_submit_sync(frame_slot, image_index, signal_value, VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    if (!submit_sync) { return std::unexpected{ vulkan::VkexecFailure(submit_sync.error()) }; }

    std::array<VkCommandBuffer, 1> const cmds{ command_buffer };
    if (auto submitted = context.vkexec_context->submit(vkexec::queue_submit{
          .command_buffers = cmds,
          .waits = submit_sync->waits,
          .signals = submit_sync->signals,
          .queue = data.graphics_queue,
        });
      !submitted) {
      return std::unexpected{ vulkan::VkexecFailure(submitted.error()) };
    }

    if (auto marked = ring.mark_submitted(frame_slot, image_index, signal_value); !marked) {
      return std::unexpected{ vulkan::VkexecFailure(marked.error()) };
    }
    if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.mark_submitted(frame_slot); }
    return {};
  }

  [[nodiscard]] auto InvokeInteractiveCapture(std::function<std::expected<CaptureOutcome, Error>()> const &capture)
    -> std::expected<CaptureOutcome, Error>
  {
    try {
      return capture();
    } catch (std::exception const &exception) {
      return std::unexpected(
        MakeError(std::errc::io_error, "interactive capture callback threw: " + std::string{ exception.what() }));
    } catch (...) {
      return std::unexpected(MakeError(std::errc::io_error, "interactive capture callback threw"));
    }
  }

}// namespace

RenderThread::~RenderThread() { Stop(); }

auto RenderThread::Start(vulkan::Context *context,
  RenderData *data,
  FrameSubmissionPolicy policy,
  bool capture_gpu_timing_samples) -> void
{
  capture_gpu_timing_samples_ = capture_gpu_timing_samples;
  StartWorker(context, data, policy, {}, {}, {}, {});
}

auto RenderThread::StartForTest(FrameSubmissionPolicy policy,
  std::function<std::expected<void, Error>()> draw_frame,
  std::function<void()> wait_idle,
  std::function<std::expected<RgbaFrame, Error>()> readback_frame,
  std::function<std::expected<EncodedPacket, Error>()> video_frame) -> void
{
  capture_gpu_timing_samples_ = false;
  StartWorker(nullptr,
    nullptr,
    policy,
    std::move(draw_frame),
    std::move(wait_idle),
    std::move(readback_frame),
    std::move(video_frame));
}

auto RenderThread::WaitForShutdownForTest() -> void
{
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return !thread_running_; });
}

auto RenderThread::WaitForBlockedProducerCountForTest(size_t count) -> void
{
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this, count]() -> bool { return blocked_producer_count_ >= count; });
}

auto RenderThread::WaitForReadbackPendingForTest() -> void
{
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return readback_pending_ || !thread_running_; });
}

auto RenderThread::WaitForCapturePendingForTest() -> void
{
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return !capture_requests_.empty() || !thread_running_; });
}

auto RenderThread::WaitForVideoCapturePendingForTest() -> void
{
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return !video_capture_requests_.empty() || !thread_running_; });
}

auto RenderThread::WaitForExclusivePendingForTest() -> void
{
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return exclusive_pending_ || !thread_running_; });
}

auto RenderThread::StartWorker(vulkan::Context *context,
  RenderData *data,
  FrameSubmissionPolicy policy,
  std::function<std::expected<void, Error>()> draw_frame,
  std::function<void()> wait_idle,
  std::function<std::expected<RgbaFrame, Error>()> readback_frame,
  std::function<std::expected<EncodedPacket, Error>()> video_frame) -> void
{
  Stop();

  context_ = context;
  data_ = data;
  draw_frame_override_ = std::move(draw_frame);
  wait_idle_override_ = std::move(wait_idle);
  readback_frame_override_ = std::move(readback_frame);
  video_frame_override_ = std::move(video_frame);

  {
    std::scoped_lock const lock{ control_mutex_ };
    thread_running_ = true;
    submission_policy_ = policy;
    idle_done_ = true;
    readback_result_.reset();
    readback_pending_ = false;
    exclusive_result_.reset();
    exclusive_action_ = {};
    exclusive_pending_ = false;
    color_target_valid_ = false;
    lfd_emulate_active_snapshot_ = data != nullptr && data->lfd_emulate_active;
    lfd_emulate_cell_snapshot_ = data != nullptr ? data->lfd_emulate_cell : std::array<u32, 2>{ 0U, 0U };
    command_ = Command::kNone;
    mailbox_.Reset();
    first_error_.reset();
    completed_frame_count_ = 0U;
    recreated_frame_count_ = 0U;
    gpu_timing_samples_.reset();
    capture_requests_.clear();
    video_capture_requests_.clear();
    worker_busy_ = false;
    blocked_producer_count_ = 0U;
    test_mode_ = static_cast<bool>(draw_frame_override_);
  }

  thread_ = std::jthread{ [this]() -> void { ThreadMain(); } };
}

void RenderThread::Stop()
{
  if (!thread_.joinable()) {
    ClearInteractiveCaptureAfterStop();
    std::scoped_lock const lock{ control_mutex_ };
    context_ = nullptr;
    data_ = nullptr;
    draw_frame_override_ = {};
    wait_idle_override_ = {};
    readback_frame_override_ = {};
    video_frame_override_ = {};
    thread_running_ = false;
    return;
  }

  {
    std::scoped_lock const lock{ control_mutex_ };
    command_ = Command::kShutdown;
    idle_done_ = false;
    thread_running_ = false;
  }
  control_cv_.notify_all();

  thread_.join();

  {
    std::scoped_lock const lock{ control_mutex_ };
    for (auto const &request : capture_requests_) {
      if (request->completed) { continue; }
      request->result =
        std::unexpected{ MakeError(std::errc::operation_canceled, "render thread stopped during capture") };
      request->completed = true;
    }
    capture_requests_.clear();
    for (auto const &request : video_capture_requests_) {
      if (request->completed) { continue; }
      request->result =
        std::unexpected{ MakeError(std::errc::operation_canceled, "render thread stopped during video capture") };
      request->completed = true;
    }
    video_capture_requests_.clear();
  }
  control_cv_.notify_all();

  ClearInteractiveCaptureAfterStop();
  context_ = nullptr;
  data_ = nullptr;
  draw_frame_override_ = {};
  wait_idle_override_ = {};
  readback_frame_override_ = {};
  video_frame_override_ = {};
}

auto RenderThread::SubmitFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<void, Error>
{
  {
    std::unique_lock lock{ control_mutex_ };
    if (submission_policy_ == FrameSubmissionPolicy::kLossless || command_ != Command::kNone || readback_pending_
        || exclusive_pending_ || !capture_requests_.empty() || !video_capture_requests_.empty()) {
      auto const can_continue = [this]() -> bool {
        return first_error_.has_value() || !thread_running_
               || (command_ == Command::kNone && !readback_pending_ && !exclusive_pending_ && capture_requests_.empty()
                   && video_capture_requests_.empty()
                   && (submission_policy_ != FrameSubmissionPolicy::kLossless || mailbox_.CanPublishLossless()));
      };
      if (!can_continue()) {
        if (test_mode_) {
          ++blocked_producer_count_;
          control_cv_.notify_all();
        }
        control_cv_.wait(lock, can_continue);
        if (test_mode_) { --blocked_producer_count_; }
      }
    }
    if (first_error_.has_value()) { return std::unexpected{ first_error_.value() }; }
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    auto const frame_slot = mailbox_.PrepareSlot();
    if (!frame_slot.has_value()) {
      return std::unexpected{ MakeError(std::errc::state_not_recoverable, "no CPU frame slot is available") };
    }
    PendingFrame frame{
      .capture = {},
      .video_capture = {},
      .aspect_ratio = aspect_ratio,
      .camera = SnapshotCamera(camera, aspect_ratio),
    };
    if (data_ != nullptr) { BuildImGuiFrameSnapshot(*data_, frame_slot.value(), frame.imgui); }
    mailbox_.Publish(frame_slot.value(), frame);
  }
  control_cv_.notify_all();

  return {};
}

auto RenderThread::CaptureFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<RgbaFrame, Error>
{
  auto request = std::make_shared<CaptureRequest>();
  {
    std::unique_lock lock{ control_mutex_ };
    control_cv_.wait(lock, [this]() -> bool {
      return first_error_.has_value() || !thread_running_
             || (command_ == Command::kNone && !readback_pending_ && !exclusive_pending_
                 && mailbox_.CanPublishLossless());
    });
    if (first_error_.has_value()) { return std::unexpected(*first_error_); }
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    auto const slot = mailbox_.PrepareSlot();
    if (!slot) { return std::unexpected{ MakeError(std::errc::state_not_recoverable, "no capture slot available") }; }
    PendingFrame const frame{ .capture = request,
      .video_capture = {},
      .aspect_ratio = aspect_ratio,
      .camera = SnapshotCamera(camera, aspect_ratio) };
    mailbox_.Publish(*slot, frame);
    capture_requests_.push_back(request);
  }
  control_cv_.notify_all();
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [&]() -> bool { return request->completed; });
  if (request->result.has_value()) { return std::move(*request->result); }
  return std::unexpected{ MakeError(std::errc::operation_canceled, "render thread stopped during capture") };
}

auto RenderThread::CaptureVideoFrame(Camera const &camera,
  f64 aspect_ratio,
  vulkan::video::GpuColorConverter &converter,
  vulkan::video::VideoEncoder &encoder,
  u64 frame_index) -> std::expected<EncodedPacket, Error>
{ return CaptureVideoFrameImpl(camera, aspect_ratio, &converter, &encoder, frame_index); }

auto RenderThread::CaptureVideoFrameForTest(Camera const &camera, f64 aspect_ratio, u64 frame_index)
  -> std::expected<EncodedPacket, Error>
{ return CaptureVideoFrameImpl(camera, aspect_ratio, nullptr, nullptr, frame_index); }

auto RenderThread::AttachInteractiveCapture(std::function<std::expected<CaptureOutcome, Error>()> capture)
  -> std::expected<u64, Error>
{
  if (!capture) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "interactive capture callback is empty"));
  }
  std::scoped_lock const lock{ control_mutex_ };
  if (interactive_capture_ || interactive_capture_callbacks_in_progress_ != 0U) {
    return std::unexpected(MakeError(
      std::errc::device_or_resource_busy, "detach the previous interactive capture before attaching another"));
  }
  interactive_capture_ = std::move(capture);
  ++interactive_capture_generation_;
  interactive_capture_active_ = true;
  interactive_capture_error_.reset();
  return interactive_capture_generation_;
}

auto RenderThread::ClearInteractiveCaptureAfterStop() -> void
{
  std::function<std::expected<CaptureOutcome, Error>()> previous;
  {
    std::scoped_lock const lock{ control_mutex_ };
    previous.swap(interactive_capture_);
    interactive_capture_active_ = false;
    ++interactive_capture_generation_;
  }
}

auto RenderThread::PollInteractiveCaptureError() const -> std::optional<Error>
{
  std::scoped_lock const lock{ control_mutex_ };
  return interactive_capture_error_;
}

auto RenderThread::DetachInteractiveCapture(u64 generation) -> std::expected<std::optional<Error>, Error>
{
  if (std::this_thread::get_id() == thread_.get_id()) {
    return std::unexpected(
      MakeError(std::errc::operation_not_permitted, "interactive capture must be detached outside the render worker"));
  }
  std::function<std::expected<CaptureOutcome, Error>()> previous;
  std::unique_lock lock{ control_mutex_ };
  if (generation != interactive_capture_generation_) {
    return std::unexpected(
      MakeError(std::errc::operation_not_permitted, "interactive capture generation was replaced"));
  }
  previous.swap(interactive_capture_);
  interactive_capture_active_ = false;
  ++interactive_capture_generation_;
  control_cv_.wait(lock, [this, generation]() -> bool {
    return interactive_capture_callbacks_in_progress_ == 0U || active_capture_generation_ != generation;
  });
  return interactive_capture_error_;
}

auto RenderThread::CaptureVideoFrameImpl(Camera const &camera,
  f64 aspect_ratio,
  vulkan::video::GpuColorConverter *converter,
  vulkan::video::VideoEncoder *encoder,
  u64 frame_index) -> std::expected<EncodedPacket, Error>
{
  auto request = std::make_shared<VideoCaptureRequest>();
  request->converter = converter;
  request->encoder = encoder;
  request->frame_index = frame_index;
  {
    std::unique_lock lock{ control_mutex_ };
    control_cv_.wait(lock, [this]() -> bool {
      return first_error_.has_value() || !thread_running_
             || (command_ == Command::kNone && !readback_pending_ && !exclusive_pending_
                 && mailbox_.CanPublishLossless());
    });
    if (first_error_.has_value()) { return std::unexpected(*first_error_); }
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    if ((converter == nullptr || encoder == nullptr) && !video_frame_override_) {
      return std::unexpected{ MakeError(
        std::errc::operation_not_supported, "CPU-only video capture requires a test callback") };
    }
    if (!video_frame_override_ && (context_ == nullptr || data_ == nullptr || context_->swapchain != nullptr)) {
      return std::unexpected{ MakeError(
        std::errc::operation_not_supported, "video capture requires an offscreen Vulkan renderer") };
    }
    auto const slot = mailbox_.PrepareSlot();
    if (!slot) {
      return std::unexpected{ MakeError(std::errc::state_not_recoverable, "no video capture slot available") };
    }
    PendingFrame const frame{
      .capture = {},
      .video_capture = request,
      .aspect_ratio = aspect_ratio,
      .camera = SnapshotCamera(camera, aspect_ratio),
    };
    mailbox_.Publish(*slot, frame);
    video_capture_requests_.push_back(request);
  }
  control_cv_.notify_all();
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [&]() -> bool { return request->completed; });
  if (request->result.has_value()) { return std::move(*request->result); }
  return std::unexpected{ MakeError(std::errc::operation_canceled, "render thread stopped during video capture") };
}

auto RenderThread::ReadLatestFrame() -> std::expected<RgbaFrame, Error>
{
  std::scoped_lock const operation_lock{ operation_mutex_ };
  {
    std::unique_lock lock{ control_mutex_ };
    if (first_error_.has_value()) { return std::unexpected(*first_error_); }
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    if ((context_ == nullptr || data_ == nullptr) && !readback_frame_override_) {
      return std::unexpected{ MakeError(
        std::errc::operation_not_supported, "GPU readback is unavailable in test mode") };
    }
    readback_pending_ = true;
    control_cv_.notify_all();
    control_cv_.wait(lock, [this]() -> bool {
      return first_error_.has_value() || !thread_running_
             || (command_ == Command::kNone && mailbox_.Empty() && !worker_busy_);
    });
    if (first_error_.has_value()) {
      readback_pending_ = false;
      control_cv_.notify_all();
      return std::unexpected(*first_error_);
    }
    if (!thread_running_) {
      readback_pending_ = false;
      control_cv_.notify_all();
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    if (!color_target_valid_) {
      readback_pending_ = false;
      control_cv_.notify_all();
      return std::unexpected{ MakeError(std::errc::invalid_argument, "current color target has no completed frame") };
    }
    readback_result_.reset();
    command_ = Command::kReadback;
  }
  control_cv_.notify_all();
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return readback_result_.has_value() || !thread_running_; });
  if (readback_result_.has_value()) { return std::move(*readback_result_); }
  return std::unexpected{ MakeError(std::errc::operation_canceled, "render thread stopped during readback") };
}

auto RenderThread::RunExclusive(std::function<std::expected<void, Error>()> action) -> std::expected<void, Error>
{
  if (!action) { return std::unexpected{ MakeError(std::errc::invalid_argument, "exclusive action is missing") }; }
  std::scoped_lock const operation_lock{ operation_mutex_ };
  {
    std::unique_lock lock{ control_mutex_ };
    if (first_error_.has_value()) { return std::unexpected(*first_error_); }
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    exclusive_pending_ = true;
    control_cv_.notify_all();
    control_cv_.wait(lock, [this]() -> bool {
      return first_error_.has_value() || !thread_running_
             || (command_ == Command::kNone && mailbox_.Empty() && !worker_busy_);
    });
    if (first_error_.has_value() || !thread_running_) {
      exclusive_pending_ = false;
      control_cv_.notify_all();
      if (first_error_.has_value()) { return std::unexpected(*first_error_); }
      return std::unexpected{ MakeError(
        std::errc::operation_canceled, "render thread stopped during reconfiguration") };
    }
    exclusive_result_.reset();
    exclusive_action_ = std::move(action);
    command_ = Command::kExclusive;
  }
  control_cv_.notify_all();
  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return exclusive_result_.has_value() || !thread_running_; });
  if (exclusive_result_.has_value()) { return std::move(*exclusive_result_); }
  return std::unexpected{ MakeError(std::errc::operation_canceled, "render thread stopped during reconfiguration") };
}

auto RenderThread::InvalidateColorTarget() -> void
{
  std::scoped_lock const lock{ control_mutex_ };
  color_target_valid_ = false;
}

auto RenderThread::LfdEmulateActive() const noexcept -> bool
{
  std::scoped_lock const lock{ control_mutex_ };
  return lfd_emulate_active_snapshot_;
}

auto RenderThread::LfdEmulateCell() const noexcept -> std::array<u32, 2>
{
  std::scoped_lock const lock{ control_mutex_ };
  return lfd_emulate_cell_snapshot_;
}

auto RenderThread::PollError() const -> std::optional<Error>
{
  std::scoped_lock const lock{ control_mutex_ };
  return first_error_;
}

auto RenderThread::CompletedFrameCount() const -> u64
{
  std::scoped_lock const lock{ control_mutex_ };
  return completed_frame_count_;
}

auto RenderThread::RecreatedFrameCount() const -> u64
{
  std::scoped_lock const lock{ control_mutex_ };
  return recreated_frame_count_;
}

auto RenderThread::WaitIdle() -> std::expected<void, Error>
{
  if (!thread_.joinable()) { return {}; }

  {
    std::unique_lock lock{ control_mutex_ };
    control_cv_.wait(lock, [this]() -> bool {
      return first_error_.has_value() || !thread_running_
             || (command_ == Command::kNone && mailbox_.Empty() && !worker_busy_);
    });
    if (first_error_.has_value()) { return std::unexpected{ first_error_.value() }; }
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    command_ = Command::kWaitIdle;
    idle_done_ = false;
  }
  control_cv_.notify_all();

  {
    std::unique_lock lock{ control_mutex_ };
    control_cv_.wait(lock, [this]() -> bool { return idle_done_ || !thread_running_ || first_error_.has_value(); });
    if (first_error_.has_value()) { return std::unexpected{ first_error_.value() }; }
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    if (data_ != nullptr) { ResetImguiFpsSamples(*data_); }
  }
  return {};
}

auto RenderThread::FinishFrame(size_t frame_slot, std::expected<FrameOutcome, Error> const &drawn) -> void
{
  {
    std::scoped_lock const lock{ control_mutex_ };
    if (!drawn && !first_error_.has_value()) {
      first_error_ = drawn.error();
      mailbox_.DiscardPending();
      for (auto const &request : capture_requests_) {
        if (!request->result.has_value()) {
          request->result = std::unexpected(*first_error_);
          request->completed = true;
        }
      }
      for (auto const &request : video_capture_requests_) {
        if (!request->result.has_value()) {
          request->result = std::unexpected(*first_error_);
          request->completed = true;
        }
      }
    }
    if (drawn) {
      if (drawn.value() == FrameOutcome::kSubmitted) {
        ++completed_frame_count_;
        color_target_valid_ = true;
      }
      if (drawn.value() == FrameOutcome::kSwapchainRecreated) {
        ++recreated_frame_count_;
        color_target_valid_ = false;
      }
    }
    mailbox_.Complete(frame_slot);
    worker_busy_ = false;
    std::erase_if(capture_requests_, [](auto const &request) -> bool { return request->result.has_value(); });
    std::erase_if(video_capture_requests_, [](auto const &request) -> bool { return request->result.has_value(); });
  }
  control_cv_.notify_all();
  if (context_ != nullptr) { context_->platform->post_empty_event(); }
}

auto RenderThread::PublishGpuTiming(std::optional<GpuFrameTimings> sample) -> void
{
  if (!capture_gpu_timing_samples_ || !sample) { return; }
  std::scoped_lock const lock{ control_mutex_ };
  gpu_timing_samples_.publish(*sample);
}

auto RenderThread::ReserveGpuTimingSamples(size_t count) -> std::expected<void, Error>
{
  std::scoped_lock const lock{ control_mutex_ };
  if (!capture_gpu_timing_samples_) {
    return std::unexpected{ MakeError(std::errc::operation_not_permitted, "GPU timing collection is disabled") };
  }
  gpu_timing_samples_.reserve(count);
  return {};
}

auto RenderThread::TakeGpuTimings() -> std::expected<std::vector<GpuFrameTimings>, Error>
{
  std::scoped_lock const lock{ control_mutex_ };
  if (!capture_gpu_timing_samples_) {
    return std::unexpected{ MakeError(std::errc::operation_not_permitted, "GPU timing collection is disabled") };
  }
  return gpu_timing_samples_.take();
}

auto RenderThread::ProcessWaitIdle(std::unique_lock<std::mutex> &lock) -> void
{
  lock.unlock();
  if (wait_idle_override_) { wait_idle_override_(); }
  auto const result = context_ != nullptr ? context_->disp.deviceWaitIdle() : VK_SUCCESS;
  std::optional<Error> timing_error;
  if (result == VK_SUCCESS && context_ != nullptr && data_ != nullptr && data_->gpu_pass_timer.enabled()) {
    for (size_t slot = 0U; slot < kFrameSlotCount; ++slot) {
      auto sample = ResolveGpuPassTimings(*context_, *data_, slot, capture_gpu_timing_samples_);
      if (!sample) {
        timing_error = sample.error();
        break;
      }
      PublishGpuTiming(*sample);
    }
  }
  lock.lock();
  bool const failed = result != VK_SUCCESS || timing_error.has_value();
  if (failed && !first_error_.has_value()) {
    first_error_ =
      timing_error.value_or(MakeError(result == VK_ERROR_DEVICE_LOST ? std::errc::no_such_device : std::errc::io_error,
        "device wait idle failed. VkResult=" + std::to_string(result)));
  }
  idle_done_ = true;
  if (command_ == Command::kWaitIdle) { command_ = Command::kNone; }
  control_cv_.notify_all();
  lock.unlock();
  if (failed && context_ != nullptr) { context_->platform->post_empty_event(); }
}

auto RenderThread::ProcessReadback(std::unique_lock<std::mutex> &lock) -> void
{
  lock.unlock();
  auto frame = ReadFrameOnWorker();
  lock.lock();
  if (!frame && frame.error().code() == std::errc::no_such_device && !first_error_) { first_error_ = frame.error(); }
  readback_result_ = std::move(frame);
  readback_pending_ = false;
  if (command_ == Command::kReadback) { command_ = Command::kNone; }
  control_cv_.notify_all();
}

auto RenderThread::ProcessExclusive(std::unique_lock<std::mutex> &lock) -> void
{
  lock.unlock();
  std::expected<void, Error> result;
  auto const wait_result = context_ != nullptr ? context_->disp.deviceWaitIdle() : VK_SUCCESS;
  if (wait_result != VK_SUCCESS) {
    result =
      std::unexpected{ MakeError(wait_result == VK_ERROR_DEVICE_LOST ? std::errc::no_such_device : std::errc::io_error,
        "failed to wait for GPU before reconfiguration") };
  } else {
    result = exclusive_action_();
  }
  lock.lock();
  if (data_ != nullptr) {
    lfd_emulate_active_snapshot_ = data_->lfd_emulate_active;
    lfd_emulate_cell_snapshot_ = data_->lfd_emulate_cell;
  }
  bool const failed = !result;
  if (failed && !first_error_.has_value()) { first_error_ = result.error(); }
  exclusive_result_ = std::move(result);
  exclusive_action_ = {};
  exclusive_pending_ = false;
  if (command_ == Command::kExclusive) { command_ = Command::kNone; }
  control_cv_.notify_all();
  lock.unlock();
  if (failed && context_ != nullptr) { context_->platform->post_empty_event(); }
  lock.lock();
}

auto RenderThread::ReadFrameOnWorker() -> std::expected<RgbaFrame, Error>
{
  if (readback_frame_override_) { return readback_frame_override_(); }
  if (context_ == nullptr || data_ == nullptr) {
    return std::unexpected{ MakeError(std::errc::operation_not_supported, "GPU readback is unavailable in test mode") };
  }
  return vulkan::ReadColorTargetRgba8(*context_, *data_);
}

auto RenderThread::ThreadMain() -> void
{
  for (;;) {
    PendingFrame frame{};
    size_t frame_slot = 0;
    {
      std::unique_lock lock{ control_mutex_ };
      control_cv_.wait(lock, [this]() -> bool { return command_ != Command::kNone || !mailbox_.Empty(); });

      if (command_ == Command::kShutdown) {
        idle_done_ = true;
        control_cv_.notify_all();
        return;
      }

      if (command_ == Command::kWaitIdle) {
        ProcessWaitIdle(lock);
        continue;
      }

      if (command_ == Command::kReadback) {
        ProcessReadback(lock);
        continue;
      }

      if (command_ == Command::kExclusive) {
        ProcessExclusive(lock);
        continue;
      }

      auto pending = mailbox_.Consume();
      if (!pending.has_value()) { continue; }
      frame_slot = pending.value().slot;
      frame = pending.value().value;
      worker_busy_ = true;
      control_cv_.notify_all();
    }

    ProcessFrame(frame_slot, frame);
  }
}

auto RenderThread::ProcessInteractiveCapture(std::expected<FrameOutcome, Error> &drawn) -> void
{
  if (!drawn || drawn.value() != FrameOutcome::kSubmitted) { return; }
  if (!test_mode_ && (context_ == nullptr || context_->swapchain == nullptr || data_ == nullptr)) { return; }

  std::function<std::expected<CaptureOutcome, Error>()> capture;
  u64 capture_generation = 0U;
  {
    std::scoped_lock const lock{ control_mutex_ };
    if (interactive_capture_active_) {
      capture = interactive_capture_;
      capture_generation = interactive_capture_generation_;
      active_capture_generation_ = capture_generation;
      ++interactive_capture_callbacks_in_progress_;
    }
  }
  if (!capture) { return; }
  auto complete_callback = [this](void *) noexcept -> void {
    {
      std::scoped_lock const lock{ control_mutex_ };
      --interactive_capture_callbacks_in_progress_;
    }
    control_cv_.notify_all();
  };
  std::unique_ptr<void, decltype(complete_callback)> const completion{ this, complete_callback };
  auto captured = InvokeInteractiveCapture(capture);
  if (captured) { return; }
  {
    std::scoped_lock const lock{ control_mutex_ };
    if (capture_generation == interactive_capture_generation_) {
      if (!interactive_capture_error_) { interactive_capture_error_ = captured.error(); }
      interactive_capture_active_ = false;
    }
  }
  if (captured.error().code() == std::errc::no_such_device) { drawn = std::unexpected(captured.error()); }
}

auto RenderThread::RecoverVideoEncodeFailure(VideoCaptureRequest const &request,
  Error const &encode_error,
  std::expected<FrameOutcome, Error> &drawn) -> std::expected<EncodedPacket, Error>
{
  std::optional<Error> cleanup_error;
  if (auto drained = request.encoder->WaitForInFlightWork(); !drained) { cleanup_error = drained.error(); }
  if (auto released = request.converter->WaitForSourceRelease(); !released) {
    if (!cleanup_error || released.error().code() == std::errc::no_such_device) { cleanup_error = released.error(); }
  }
  if (cleanup_error) {
    auto const &failure = encode_error.code() == std::errc::no_such_device ? encode_error : *cleanup_error;
    drawn = std::unexpected(failure);
    return std::unexpected(failure);
  }
  return std::unexpected(encode_error);
}

auto RenderThread::EncodeVideoCapture(VideoCaptureRequest const &request, std::expected<FrameOutcome, Error> &drawn)
  -> std::expected<EncodedPacket, Error>
{
  if (!drawn) { return std::unexpected(drawn.error()); }
  if (video_frame_override_) { return video_frame_override_(); }
  if (drawn.value() != FrameOutcome::kSubmitted || context_ == nullptr || data_ == nullptr
      || !data_->color_image.has_value() || data_->color_format != VK_FORMAT_R8G8B8A8_UNORM) {
    return std::unexpected(
      MakeError(std::errc::operation_not_supported, "video capture requires a rendered RGBA8 color target"));
  }
  auto const extent = request.converter->Extent();
  if (data_->color_width != extent.width || data_->color_height != extent.height) {
    return std::unexpected(
      MakeError(std::errc::invalid_argument, "video converter extent does not match the rendered color target"));
  }
  auto prepared =
    request.converter->Convert(data_->color_image->handle(), data_->offscreen_timeline, data_->offscreen_signal_value);
  if (!prepared) { return std::unexpected(prepared.error()); }
  auto packet = request.encoder->EncodeImage(*prepared, request.frame_index);
  if (!packet) { return RecoverVideoEncodeFailure(request, packet.error(), drawn); }
  if (auto completed = request.converter->Complete(); !completed) {
    drawn = std::unexpected(completed.error());
    return std::unexpected(completed.error());
  }
  return packet;
}

auto RenderThread::ProcessVideoCapture(PendingFrame const &frame, std::expected<FrameOutcome, Error> &drawn) -> void
{
  if (frame.video_capture == nullptr) { return; }
  // The worker owns both the graphics queue and the shared color target here.
  // EncodeImage blocks until the converted image has been consumed, so the
  // next frame cannot overwrite either image before this request completes.
  auto encoded = EncodeVideoCapture(*frame.video_capture, drawn);
  if (!encoded) {
    if (encoded.error().code() == std::errc::no_such_device) { drawn = std::unexpected(encoded.error()); }
    if (frame.video_capture->encoder != nullptr) { frame.video_capture->encoder->Abort(); }
    if (frame.video_capture->converter != nullptr) { frame.video_capture->converter->Abort(); }
  }
  {
    std::scoped_lock const lock{ control_mutex_ };
    frame.video_capture->result = std::move(encoded);
  }
}

// Coordinates draw submission with independent asynchronous capture callbacks.
auto RenderThread::ProcessFrame(size_t frame_slot, PendingFrame const &frame) -> void
{
  if (data_ != nullptr) {
    data_->frames.at(frame_slot).setup = FrameSetup{
      .aspect_ratio = frame.aspect_ratio,
      .camera = frame.camera,
      .imgui = frame.imgui,
    };
  }
  auto drawn = draw_frame_override_
                 ? draw_frame_override_().transform([]() -> FrameOutcome { return FrameOutcome::kSubmitted; })
                 : DrawFrameVulkan(frame_slot);

  ProcessInteractiveCapture(drawn);

  if (frame.capture != nullptr) {
    auto captured = drawn && drawn.value() == FrameOutcome::kSubmitted
                      ? ReadFrameOnWorker()
                      : std::expected<RgbaFrame, Error>{ std::unexpected(
                          drawn ? MakeError(std::errc::io_error, "capture frame was skipped") : drawn.error()) };
    if (!captured) { drawn = std::unexpected(captured.error()); }
    {
      std::scoped_lock const lock{ control_mutex_ };
      frame.capture->result = std::move(captured);
    }
  }

  ProcessVideoCapture(frame, drawn);

  FinishFrame(frame_slot, drawn);
  if (frame.capture != nullptr) {
    {
      std::scoped_lock const lock{ control_mutex_ };
      frame.capture->completed = true;
    }
    control_cv_.notify_all();
  }
  if (frame.video_capture != nullptr) {
    {
      std::scoped_lock const lock{ control_mutex_ };
      frame.video_capture->completed = true;
    }
    control_cv_.notify_all();
  }
}

// Coordinates swapchain acquisition, rendering, presentation, and capture.
auto RenderThread::DrawFrameVulkan(size_t frame_slot) -> std::expected<FrameOutcome, Error>
{
  if (context_ == nullptr || data_ == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "Vulkan render state is not initialized") };
  }
  vulkan::Context const &context = *context_;
  RenderData &data = *data_;
  data.current_slot = frame_slot;

  if (context.swapchain == nullptr) { return DrawFrameOffscreen(frame_slot); }

  if (!data.frame_ring.has_value() || context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "frame_ring is not initialized") };
  }
  auto &ring = *data.frame_ring;

  auto waited = WaitFrameResources(context, data, ring, frame_slot, capture_gpu_timing_samples_);
  if (!waited) { return std::unexpected{ waited.error() }; }
  PublishGpuTiming(*waited);

  auto const extent = context.platform->framebuffer_extent();
  if (extent.width == 0 || extent.height == 0) { return FrameOutcome::kSkipped; }

  return DrawSwapchainFrame(frame_slot);
}

auto RenderThread::DrawSwapchainFrame(size_t frame_slot) -> std::expected<FrameOutcome, Error>
{
  vulkan::Context &context = *context_;
  RenderData &data = *data_;
  if (!data.frame_ring.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "frame_ring is not initialized") };
  }
  auto &ring = *data.frame_ring;
  auto acquire_semaphore = ring.acquire_semaphore(frame_slot);
  if (!acquire_semaphore) { return std::unexpected{ vulkan::VkexecFailure(acquire_semaphore.error()) }; }

  uint32_t image_index = 0;
  VkResult result = context.disp.acquireNextImageKHR(
    context.swapchain->handle(), UINT64_MAX, *acquire_semaphore, VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    if (auto recreated = RecreateSwapchain(context, data); !recreated) { return std::unexpected{ recreated.error() }; }
    return FrameOutcome::kSwapchainRecreated;
  }
  if (result == VK_TIMEOUT || result == VK_NOT_READY) { return FrameOutcome::kSkipped; }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return std::unexpected{ MakeError(result == VK_ERROR_DEVICE_LOST ? std::errc::no_such_device : std::errc::io_error,
      "failed to acquire swapchain image. VkResult=" + std::to_string(result)) };
  }
  bool const acquired_suboptimal = result == VK_SUBOPTIMAL_KHR;

  if (auto submitted = RecordAndSubmitFrame(context, data, ring, frame_slot, image_index); !submitted) {
    auto const retired = RetireAcquiredImage(context, data, submitted.error());
    return std::unexpected{ retired.error() };
  }

  auto finished_semaphore = ring.render_finished_semaphore(image_index);
  if (!finished_semaphore) {
    auto const retired = RetireAcquiredImage(context, data, vulkan::VkexecFailure(finished_semaphore.error()));
    return std::unexpected{ retired.error() };
  }

  auto presented = PresentSwapchainImage(context, data, image_index, *finished_semaphore);
  if (!presented) {
    auto const retired = RetireAcquiredImage(context, data, presented.error());
    return std::unexpected{ retired.error() };
  }
  result = *presented;

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR
      || (result == VK_SUCCESS && acquired_suboptimal)) {
    if (auto recreated = RecreateSwapchain(context, data); !recreated) { return std::unexpected{ recreated.error() }; }
    return FrameOutcome::kSwapchainRecreated;
  }
  if (result != VK_SUCCESS) {
    auto const retired = RetireAcquiredImage(context,
      data,
      MakeError(result == VK_ERROR_DEVICE_LOST ? std::errc::no_such_device : std::errc::io_error,
        "failed to present swapchain image. VkResult=" + std::to_string(result)));
    return std::unexpected{ retired.error() };
  }

  return FrameOutcome::kSubmitted;
}

auto RenderThread::DrawFrameOffscreen(size_t frame_slot) -> std::expected<FrameOutcome, Error>
{
  vulkan::Context &context = *context_;
  RenderData &data = *data_;
  if (data.offscreen_timeline == VK_NULL_HANDLE || context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "offscreen timeline is missing") };
  }
  if (data.offscreen_signal_value != 0U) {
    VkSemaphoreWaitInfo const wait_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .pNext = nullptr,
      .flags = 0U,
      .semaphoreCount = 1U,
      .pSemaphores = &data.offscreen_timeline,
      .pValues = &data.offscreen_signal_value,
    };
    auto const waited = context.disp.waitSemaphores(&wait_info, UINT64_MAX);
    if (waited != VK_SUCCESS) {
      return std::unexpected{ MakeError(
        waited == VK_ERROR_DEVICE_LOST ? std::errc::no_such_device : std::errc::io_error,
        "failed to wait for offscreen frame") };
    }
  }
  auto resolved = ResolveGpuPassTimings(context, data, frame_slot, capture_gpu_timing_samples_);
  if (!resolved) { return std::unexpected(resolved.error()); }
  PublishGpuTiming(*resolved);

  FrameSetup &setup = data.frames.at(frame_slot).setup;
  BuildFrameSetupGpu(data, setup, frame_slot);
  ApplyFrameSetup(data, frame_slot);
  auto *command_buffer = data.command_buffers.at(frame_slot).handle();
  auto recorded = vulkan::WithCommand(std::ref(context.disp),
    command_buffer,
    [&](vkb::DispatchTable &, VkCommandBuffer cmd) -> std::expected<void, Error> {
      BindDescriptorHeap(context, data, cmd);
      if (auto result = gs::EvalGsPipeline(context, data, cmd); !result) { return result; }
      data.gpu_pass_timer.end_frame(context, frame_slot, cmd);
      return {};
    });
  if (!recorded) { return std::unexpected(recorded.error()); }

  u64 const signal_value = ++data.offscreen_signal_value;
  std::array<vkexec::semaphore_submit, 1> const signals{ vkexec::semaphore_submit{
    .semaphore = data.offscreen_timeline,
    .value = signal_value,
    .stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
  } };
  std::array<VkCommandBuffer, 1> const cmds{ command_buffer };
  if (auto submitted = context.vkexec_context->submit(vkexec::queue_submit{
        .command_buffers = cmds,
        .signals = signals,
        .queue = data.graphics_queue,
      });
    !submitted) {
    --data.offscreen_signal_value;
    return std::unexpected{ vulkan::VkexecFailure(submitted.error()) };
  }
  if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.mark_submitted(frame_slot); }
  return FrameOutcome::kSubmitted;
}

}// namespace vkgsplat
