#include "render_thread.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "app_state.hpp"
#include "gs/pipeline.hpp"
#include "vulkan/command/command.hpp"
#include "vulkan/descriptor/descriptor_heap.hpp"
#include "vulkan/frame_context.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "vulkan/initializers.hpp"
#include "vulkan/renderer.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/camera.hpp>
#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

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
      return std::unexpected{ MakeError(std::errc::io_error, waited.error().message()) };
    }
    // Gaussian buffers and the color target are shared across frame slots.
    size_t const previous_slot = (frame_slot + kFrameSlotCount - 1U) % kFrameSlotCount;
    if (auto waited = ring.wait_slot(previous_slot); !waited) {
      return std::unexpected{ MakeError(std::errc::io_error, waited.error().message()) };
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
      return std::unexpected{ MakeError(std::errc::io_error, waited.error().message()) };
    }

    FrameSetup &setup = data.frames.at(frame_slot).setup;
    BuildFrameSetupGpu(context, data, setup, image_index);
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
    if (!submit_sync) { return std::unexpected{ MakeError(std::errc::io_error, submit_sync.error().message()) }; }

    std::array<VkCommandBuffer, 1> const cmds{ command_buffer };
    if (auto submitted = context.vkexec_context->submit(vkexec::queue_submit{
          .command_buffers = cmds,
          .waits = submit_sync->waits,
          .signals = submit_sync->signals,
          .queue = data.graphics_queue,
        });
      !submitted) {
      return std::unexpected{ MakeError(std::errc::io_error, submitted.error().message()) };
    }

    if (auto marked = ring.mark_submitted(frame_slot, image_index, signal_value); !marked) {
      return std::unexpected{ MakeError(std::errc::io_error, marked.error().message()) };
    }
    if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.mark_submitted(frame_slot); }
    return {};
  }

}// namespace

RenderThread::~RenderThread() { Stop(); }

auto RenderThread::Start(vulkan::Context *context,
  RenderData *data,
  FrameSubmissionPolicy policy,
  bool capture_gpu_timing_samples) -> void
{
  capture_gpu_timing_samples_ = capture_gpu_timing_samples;
  StartWorker(context, data, policy, {}, {});
}

auto RenderThread::StartForTest(FrameSubmissionPolicy policy,
  std::function<std::expected<void, Error>()> draw_frame,
  std::function<void()> wait_idle) -> void
{
  capture_gpu_timing_samples_ = false;
  StartWorker(nullptr, nullptr, policy, std::move(draw_frame), std::move(wait_idle));
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

auto RenderThread::StartWorker(vulkan::Context *context,
  RenderData *data,
  FrameSubmissionPolicy policy,
  std::function<std::expected<void, Error>()> draw_frame,
  std::function<void()> wait_idle) -> void
{
  Stop();

  context_ = context;
  data_ = data;
  draw_frame_override_ = std::move(draw_frame);
  wait_idle_override_ = std::move(wait_idle);

  {
    std::scoped_lock const lock{ control_mutex_ };
    thread_running_ = true;
    submission_policy_ = policy;
    idle_done_ = true;
    command_ = Command::kNone;
    mailbox_.Reset();
    first_error_.reset();
    completed_frame_count_ = 0U;
    recreated_frame_count_ = 0U;
    gpu_timing_samples_.reset();
    worker_busy_ = false;
    blocked_producer_count_ = 0U;
    test_mode_ = static_cast<bool>(draw_frame_override_);
  }

  thread_ = std::jthread{ [this]() -> void { ThreadMain(); } };
}

void RenderThread::Stop()
{
  if (!thread_.joinable()) {
    std::scoped_lock const lock{ control_mutex_ };
    context_ = nullptr;
    data_ = nullptr;
    draw_frame_override_ = {};
    wait_idle_override_ = {};
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

  context_ = nullptr;
  data_ = nullptr;
  draw_frame_override_ = {};
  wait_idle_override_ = {};
}

auto RenderThread::SubmitFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<void, Error>
{
  {
    std::unique_lock lock{ control_mutex_ };
    if (submission_policy_ == FrameSubmissionPolicy::kLossless) {
      auto const can_continue = [this]() -> bool {
        return first_error_.has_value() || !thread_running_
               || (command_ == Command::kNone && mailbox_.CanPublishLossless());
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
    PendingFrame frame{ .aspect_ratio = aspect_ratio, .camera = SnapshotCamera(camera, aspect_ratio) };
    if (data_ != nullptr) { BuildImGuiFrameSnapshot(*data_, frame_slot.value(), frame.imgui); }
    mailbox_.Publish(frame_slot.value(), frame);
  }
  control_cv_.notify_all();

  return {};
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
    }
    if (drawn) {
      if (drawn.value() == FrameOutcome::kSubmitted) { ++completed_frame_count_; }
      if (drawn.value() == FrameOutcome::kSwapchainRecreated) { ++recreated_frame_count_; }
    }
    mailbox_.Complete(frame_slot);
    worker_busy_ = false;
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
    first_error_ = timing_error.value_or(
      MakeError(std::errc::io_error, "device wait idle failed. VkResult=" + std::to_string(result)));
  }
  idle_done_ = true;
  if (command_ == Command::kWaitIdle) { command_ = Command::kNone; }
  control_cv_.notify_all();
  lock.unlock();
  if (failed && context_ != nullptr) { context_->platform->post_empty_event(); }
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

      auto pending = mailbox_.Consume();
      if (!pending.has_value()) { continue; }
      frame_slot = pending.value().slot;
      frame = pending.value().value;
      worker_busy_ = true;
      control_cv_.notify_all();
    }

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

    FinishFrame(frame_slot, drawn);
  }
}

auto RenderThread::DrawFrameVulkan(size_t frame_slot) -> std::expected<FrameOutcome, Error>
{
  vulkan::Context &context = *context_;
  RenderData &data = *data_;
  data.current_slot = frame_slot;

  if (!data.frame_ring.has_value() || context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "frame_ring is not initialized") };
  }
  auto &ring = *data.frame_ring;

  auto waited = WaitFrameResources(context, data, ring, frame_slot, capture_gpu_timing_samples_);
  if (!waited) { return std::unexpected{ waited.error() }; }
  PublishGpuTiming(*waited);

  auto const extent = context.platform->framebuffer_extent();
  if (extent.width == 0 || extent.height == 0) { return FrameOutcome::kSkipped; }

  auto acquire_semaphore = ring.acquire_semaphore(frame_slot);
  if (!acquire_semaphore) {
    return std::unexpected{ MakeError(std::errc::io_error, acquire_semaphore.error().message()) };
  }

  uint32_t image_index = 0;
  VkResult result = context.disp.acquireNextImageKHR(
    context.swapchain->handle(), UINT64_MAX, *acquire_semaphore, VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    if (auto recreated = RecreateSwapchain(context, data); !recreated) { return std::unexpected{ recreated.error() }; }
    return FrameOutcome::kSwapchainRecreated;
  }
  if (result == VK_TIMEOUT || result == VK_NOT_READY) { return FrameOutcome::kSkipped; }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return std::unexpected{ MakeError(
      std::errc::io_error, "failed to acquire swapchain image. VkResult=" + std::to_string(result)) };
  }
  bool const acquired_suboptimal = result == VK_SUBOPTIMAL_KHR;

  if (auto submitted = RecordAndSubmitFrame(context, data, ring, frame_slot, image_index); !submitted) {
    auto const retired = RetireAcquiredImage(context, data, submitted.error());
    return std::unexpected{ retired.error() };
  }

  auto finished_semaphore = ring.render_finished_semaphore(image_index);
  if (!finished_semaphore) {
    auto const retired =
      RetireAcquiredImage(context, data, MakeError(std::errc::io_error, finished_semaphore.error().message()));
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
      MakeError(std::errc::io_error, "failed to present swapchain image. VkResult=" + std::to_string(result)));
    return std::unexpected{ retired.error() };
  }

  return FrameOutcome::kSubmitted;
}

}// namespace vkgsplat
