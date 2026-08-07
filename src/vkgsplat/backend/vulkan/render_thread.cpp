#include "render_thread.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <print>
#include <span>
#include <string>
#include <system_error>

#include "app_state.hpp"
#include "backend/vulkan/command/command.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "backend/vulkan/frame_context.hpp"
#include "backend/vulkan/gpu_pass_timer.hpp"
#include "backend/vulkan/imgui_overlay.hpp"
#include "backend/vulkan/initializers.hpp"
#include "backend/vulkan/renderer.hpp"
#include "gs/pipeline.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace {

  void RecordSphereDraw(Init &init, RenderData &data, VkCommandBuffer command_buffer, size_t image_index)
  {
    BindDescriptorHeap(init, data, command_buffer);
    gs::EvalGsPipeline(init, data, command_buffer);
    RecordImguiOverlay(init, data, command_buffer, image_index);
  }

}// namespace

RenderThread::~RenderThread() { Stop(); }

void RenderThread::Start(Init *init, RenderData *data)
{
  Stop();

  init_ = init;
  data_ = data;

  {
    std::scoped_lock const lock{ mutex_ };
    thread_running_ = true;
    work_done_ = true;
    command_ = Command::kNone;
    work_error_.reset();
  }

  thread_ = std::jthread{ [this]() -> void { ThreadMain(); } };
}

void RenderThread::Stop()
{
  if (!thread_.joinable()) {
    init_ = nullptr;
    data_ = nullptr;
    thread_running_ = false;
    return;
  }

  {
    std::scoped_lock const lock{ mutex_ };
    command_ = Command::kShutdown;
    work_done_ = false;
  }
  work_cv_.notify_one();
  thread_.join();

  init_ = nullptr;
  data_ = nullptr;
  thread_running_ = false;
}

auto RenderThread::SubmitFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<void, Error>
{
  size_t frame_slot = 0;
  {
    std::unique_lock lock{ mutex_ };
    if (!thread_running_) {
      return std::unexpected{ MakeError(std::errc::operation_not_permitted, "render thread is not running") };
    }
    done_cv_.wait(lock, [this]() -> bool { return work_done_; });
    frame_slot = data_->current_frame;
  }

  BuildFrameSetupCpu(*data_, frame_slot, PrepareFrameParams{ .camera = &camera, .aspect_ratio = aspect_ratio });

  {
    std::scoped_lock const lock{ mutex_ };
    command_ = Command::kDraw;
    work_done_ = false;
    work_error_.reset();
  }
  work_cv_.notify_one();

  std::unique_lock lock{ mutex_ };
  done_cv_.wait(lock, [this]() -> bool { return work_done_; });

  if (work_error_) { return std::unexpected{ *work_error_ }; }
  return {};
}

void RenderThread::WaitIdle()
{
  if (!thread_.joinable()) { return; }

  {
    std::scoped_lock const lock{ mutex_ };
    if (!thread_running_) { return; }
  }

  {
    std::unique_lock lock{ mutex_ };
    done_cv_.wait(lock, [this]() -> bool { return work_done_; });
    command_ = Command::kWaitIdle;
    work_done_ = false;
    work_error_.reset();
  }
  work_cv_.notify_one();

  std::unique_lock lock{ mutex_ };
  done_cv_.wait(lock, [this]() -> bool { return work_done_; });
}

void RenderThread::ThreadMain()
{
  for (;;) {
    std::unique_lock lock{ mutex_ };
    work_cv_.wait(lock, [this]() -> bool { return command_ != Command::kNone; });
    Command const command = command_;
    command_ = Command::kNone;
    lock.unlock();

    if (command == Command::kShutdown) {
      std::scoped_lock const done_lock{ mutex_ };
      work_done_ = true;
      done_cv_.notify_one();
      return;
    }

    if (command == Command::kWaitIdle) {
      init_->disp.deviceWaitIdle();
      std::scoped_lock const done_lock{ mutex_ };
      work_done_ = true;
      done_cv_.notify_one();
      continue;
    }

    if (command == Command::kDraw) {
      if (auto drawn = DrawFrameVulkan(); !drawn) { work_error_ = drawn.error(); }

      std::scoped_lock const done_lock{ mutex_ };
      work_done_ = true;
      done_cv_.notify_one();
    }
  }
}

auto RenderThread::DrawFrameVulkan() -> std::expected<void, Error>
{
  Init &init = *init_;
  RenderData &data = *data_;

  auto *in_flight_fence = data.in_flight_fences.at(data.current_frame).handle();
  init.disp.waitForFences(1, &in_flight_fence, VK_TRUE, UINT64_MAX);

  if (data.gpu_pass_timer.enabled()) {
    data.gpu_pass_timer.resolve(init, data.current_frame);
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
  auto *available_semaphore = data.available_semaphores.at(data.current_frame).handle();
  VkResult result = init.disp.acquireNextImageKHR(
    init.swapchain->handle(), UINT64_MAX, available_semaphore, VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return RecreateSwapchain(init, data);
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    return std::unexpected{ MakeError(
      std::errc::io_error, "failed to acquire swapchain image. VkResult=" + std::to_string(result)) };
  }

  if (data.image_in_flight.at(image_index) != VK_NULL_HANDLE) {
    init.disp.waitForFences(1, &data.image_in_flight.at(image_index), VK_TRUE, UINT64_MAX);
  }
  data.image_in_flight.at(image_index) = in_flight_fence;

  size_t const frame_slot = data.current_frame;
  FrameSetup &setup = data.frames.at(frame_slot).setup;
  BuildFrameSetupGpu(init, data, setup, image_index);
  ApplyFrameSetup(data, frame_slot);

  auto recorded = vulkan::WithCommand(std::ref(init.disp),
    data.command_buffers.at(image_index).handle(),
    [&](vkb::DispatchTable &, VkCommandBuffer cmd) -> void {
      RecordSphereDraw(init, data, cmd, image_index);
    });
  if (!recorded) { return std::unexpected{ recorded.error() }; }

  std::array<VkSemaphore, 1> wait_semaphores = { available_semaphore };
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  auto *finished_semaphore = data.finished_semaphore.at(image_index).handle();
  std::array<VkSemaphore, 1> signal_semaphores = { finished_semaphore };

  auto *command_buffer = data.command_buffers.at(image_index).handle();
  auto const submit_info =
    initializers::SubmitInfo(wait_semaphores, wait_stages, std::span{ &command_buffer, 1 }, signal_semaphores);

  init.disp.resetFences(1, &in_flight_fence);

  if (init.disp.queueSubmit(data.graphics_queue, 1, &submit_info, in_flight_fence) != VK_SUCCESS) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to submit draw command buffer") };
  }

  std::array<VkSwapchainKHR, 1> const swap_chains = { init.swapchain->handle() };
  auto const present_info = initializers::PresentInfoKHR(signal_semaphores, swap_chains, std::span{ &image_index, 1 });

  result = init.disp.queuePresentKHR(data.present_queue, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return RecreateSwapchain(init, data);
  }
  if (result != VK_SUCCESS) {
    return std::unexpected{ MakeError(std::errc::io_error, "failed to present swapchain image") };
  }

  data.current_frame = (data.current_frame + 1) % kMaxFramesInFlight;
  return {};
}

}// namespace vkgsplat
