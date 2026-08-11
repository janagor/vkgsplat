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

#include "app_state.hpp"
#include "vulkan/command/command.hpp"
#include "vulkan/descriptor/descriptor_heap.hpp"
#include "vulkan/frame_context.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "vulkan/initializers.hpp"
#include "vulkan/renderer.hpp"
#include "gs/pipeline.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace {

  void RecordSphereDraw(vulkan::Context &context, RenderData &data, VkCommandBuffer command_buffer, size_t image_index)
  {
    BindDescriptorHeap(context, data, command_buffer);
    gs::EvalGsPipeline(context, data, command_buffer);
    RecordImguiOverlay(context, data, command_buffer, image_index);
  }

}// namespace

RenderThread::~RenderThread() { Stop(); }

void RenderThread::Start(vulkan::Context *context, RenderData *data)
{
  Stop();

  context_ = context;
  data_ = data;

  pending_queue_.Clear();
  completion_queue_.Clear();
  MarkAllSlotsReady();

  {
    std::scoped_lock const lock{ control_mutex_ };
    thread_running_ = true;
    idle_done_ = true;
    command_ = Command::kNone;
    submit_serial_ = 0;
  }

  thread_ = std::jthread{ [this]() -> void { ThreadMain(); } };
}

void RenderThread::Stop()
{
  if (!thread_.joinable()) {
    context_ = nullptr;
    data_ = nullptr;
    thread_running_ = false;
    return;
  }

  {
    std::scoped_lock const lock{ control_mutex_ };
    command_ = Command::kShutdown;
    idle_done_ = false;
  }
  control_cv_.notify_one();

  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return idle_done_; });
  thread_.join();

  context_ = nullptr;
  data_ = nullptr;
  thread_running_ = false;
}

auto RenderThread::DrainCompletions() -> std::optional<Error>
{
  std::optional<Error> first_error{};
  while (auto const completion = completion_queue_.TryPop()) {
    if (completion->error.has_value() && !first_error.has_value()) { first_error = completion->error; }
  }
  return first_error;
}

void RenderThread::EnqueueCompletion(FrameCompletion const &completion)
{
  FrameCompletion pending = completion;
  while (!completion_queue_.TryPush(std::move(pending))) {
    static_cast<void>(completion_queue_.TryPop());
    pending = completion;
  }
}

void RenderThread::WaitSlotReady(size_t slot)
{
  std::unique_lock lock{ slot_mutex_ };
  slot_ready_cv_.wait(lock, [this, slot]() -> bool { return slot_ready_.at(slot); });
  slot_ready_.at(slot) = false;
}

void RenderThread::MarkSlotReady(size_t slot)
{
  std::scoped_lock const lock{ slot_mutex_ };
  slot_ready_.at(slot) = true;
  slot_ready_cv_.notify_all();
}

void RenderThread::MarkAllSlotsReady()
{
  std::scoped_lock const lock{ slot_mutex_ };
  slot_ready_.fill(true);
  slot_ready_cv_.notify_all();
}

auto RenderThread::SubmitFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<void, Error>
{
  if (auto const prior_error = DrainCompletions(); prior_error.has_value()) {
    return std::unexpected{ *prior_error };
  }

  {
    std::scoped_lock const lock{ control_mutex_ };
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
  }

  pending_queue_.WaitNotFull();

  size_t const frame_slot = submit_serial_ % PendingQueue::kCapacity;
  ++submit_serial_;

  WaitSlotReady(frame_slot);

  BuildFrameSetupCpu(*data_, frame_slot, PrepareFrameParams{ .camera = &camera, .aspect_ratio = aspect_ratio });

  pending_queue_.Push(frame_slot);
  control_cv_.notify_one();

  return {};
}

void RenderThread::WaitIdle()
{
  if (!thread_.joinable()) { return; }

  static_cast<void>(DrainCompletions());

  pending_queue_.WaitEmpty();

  {
    std::scoped_lock const lock{ control_mutex_ };
    if (!thread_running_) { return; }
    command_ = Command::kWaitIdle;
    idle_done_ = false;
  }
  control_cv_.notify_one();

  std::unique_lock lock{ control_mutex_ };
  control_cv_.wait(lock, [this]() -> bool { return idle_done_; });

  completion_queue_.Clear();
}

void RenderThread::ThreadMain()
{
  for (;;) {
    size_t frame_slot = 0;
    if (!pending_queue_.TryPop(frame_slot)) {
      std::unique_lock lock{ control_mutex_ };
      control_cv_.wait(lock, [this]() -> bool {
        return command_ != Command::kNone || !pending_queue_.Empty();
      });

      if (command_ == Command::kShutdown) {
        idle_done_ = true;
        control_cv_.notify_one();
        return;
      }

      if (command_ == Command::kWaitIdle) {
        lock.unlock();
        context_->disp.deviceWaitIdle();
        MarkAllSlotsReady();
        lock.lock();
        idle_done_ = true;
        command_ = Command::kNone;
        control_cv_.notify_one();
      }

      continue;
    }

    std::optional<Error> frame_error{};
    if (auto drawn = DrawFrameVulkan(frame_slot); !drawn) { frame_error = drawn.error(); }

    // FrameSetup / per-slot CPU state may be rebuilt once recording+submit finished.
    MarkSlotReady(frame_slot);
    EnqueueCompletion(FrameCompletion{ .error = std::move(frame_error) });
  }
}

auto RenderThread::DrawFrameVulkan(size_t frame_slot) -> std::expected<void, Error>
{
  vulkan::Context &context = *context_;
  RenderData &data = *data_;
  data.current_slot = frame_slot;

  if (!data.frame_timeline.has_value()) {
    return std::unexpected{ MakeError(std::errc::state_not_recoverable, "frame timeline semaphore is not initialized") };
  }

  if (auto waited = data.frame_timeline->wait_value(data.slot_timeline_value.at(frame_slot)); !waited) {
    return std::unexpected{ waited.error() };
  }

  if (data.gpu_pass_timer.enabled()) {
    data.gpu_pass_timer.resolve(context, frame_slot);
    if (data.imgui != nullptr) {
      UpdateImguiGpuTimings(data);
    } else {
      static auto last_print = std::chrono::steady_clock::time_point{};
      auto const now = std::chrono::steady_clock::now();
      if (last_print.time_since_epoch().count() == 0 || now - last_print >= std::chrono::seconds{ 1 }) {
        last_print = now;
        auto const &pass_ms = data.gpu_pass_timer.last_ms();
        std::println("GPU: {:.2f} ms (proj {:.2f} bin {:.2f} prep {:.2f} radix {:.2f} raster {:.2f})",
          data.gpu_pass_timer.total_ms(),
          pass_ms.at(static_cast<size_t>(GpuPass::kProjection)),
          pass_ms.at(static_cast<size_t>(GpuPass::kBinning)),
          pass_ms.at(static_cast<size_t>(GpuPass::kPrepareSort)),
          pass_ms.at(static_cast<size_t>(GpuPass::kRadixSort)),
          pass_ms.at(static_cast<size_t>(GpuPass::kRasterize)));
      }
    }
  }

  uint32_t image_index = 0;
  auto *available_semaphore = data.available_semaphores.at(frame_slot).handle();
  VkResult result = context.disp.acquireNextImageKHR(
    context.swapchain->handle(), UINT64_MAX, available_semaphore, VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    if (auto recreated = RecreateSwapchain(context, data); !recreated) { return std::unexpected{ recreated.error() }; }
    MarkAllSlotsReady();
    return {};
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return std::unexpected{ MakeError(
      std::errc::io_error, "failed to acquire swapchain image. VkResult=" + std::to_string(result)) };
  }

  if (auto waited = data.frame_timeline->wait_value(data.image_timeline_value.at(image_index)); !waited) {
    return std::unexpected{ waited.error() };
  }

  FrameSetup &setup = data.frames.at(frame_slot).setup;
  BuildFrameSetupGpu(context, data, setup, image_index);
  ApplyFrameSetup(data, frame_slot);

  auto recorded = vulkan::WithCommand(std::ref(context.disp),
    data.command_buffers.at(image_index).handle(),
    [&](vkb::DispatchTable &, VkCommandBuffer cmd) -> void {
      RecordSphereDraw(context, data, cmd, image_index);
    });
  if (!recorded) { return std::unexpected{ recorded.error() }; }

  auto const signal_value = data.next_timeline_value + 1U;
  data.next_timeline_value = signal_value;

  std::array<VkSemaphore, 1> wait_semaphores = { available_semaphore };
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  auto *finished_semaphore = data.finished_semaphore.at(image_index).handle();
  std::array<VkSemaphore, 2> signal_semaphores = { finished_semaphore, data.frame_timeline->handle() };
  std::array<u64, 1> const wait_values{ 0 };
  std::array<u64, 2> const signal_values{ 0, signal_value };
  auto timeline_submit = initializers::TimelineSemaphoreSubmitInfo(wait_values, signal_values);

  auto *command_buffer = data.command_buffers.at(image_index).handle();
  auto submit_info =
    initializers::SubmitInfo(wait_semaphores, wait_stages, std::span{ &command_buffer, 1 }, signal_semaphores);
  submit_info.pNext = &timeline_submit;

  if (context.disp.queueSubmit(data.graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to submit draw command buffer") };
  }

  data.slot_timeline_value.at(frame_slot) = signal_value;
  data.image_timeline_value.at(image_index) = signal_value;

  std::array<VkSwapchainKHR, 1> const swap_chains = { context.swapchain->handle() };
  std::array<VkSemaphore, 1> present_wait = { finished_semaphore };
  auto present_info = initializers::PresentInfoKHR(present_wait, swap_chains, std::span{ &image_index, 1 });

  if (data.present_pacer != nullptr) { data.present_pacer->PreparePresent(context, present_info); }

  result = context.disp.queuePresentKHR(data.present_queue, &present_info);

  if (data.present_pacer != nullptr) { data.present_pacer->AfterPresent(context); }

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    if (auto recreated = RecreateSwapchain(context, data); !recreated) { return std::unexpected{ recreated.error() }; }
    MarkAllSlotsReady();
    return {};
  }
  if (result != VK_SUCCESS) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to present swapchain image") };
  }

  return {};
}

}// namespace vkgsplat
