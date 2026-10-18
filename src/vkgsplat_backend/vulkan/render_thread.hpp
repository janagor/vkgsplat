#ifndef VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP
#define VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP

#include "app_state.hpp"
#include <vkgsplat_adt/latest_frame_mailbox.hpp>

#include <condition_variable>
#include <cstddef>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_backend/vkgsplat_backend_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

namespace vulkan {
  struct Context;
}
struct RenderData;

// Dedicated thread for all Vulkan queue, swapchain, and command-buffer work.
// Main publishes one replaceable camera and UI snapshot while the worker owns a slot.
// GPU completion for a slot/image is tracked on frame_ring and waited on the worker.
class VKGSPLAT_BACKEND_EXPORT RenderThread
{
public:
  RenderThread() = default;
  ~RenderThread();

  RenderThread(RenderThread const &) = delete;
  auto operator=(RenderThread const &) -> RenderThread & = delete;
  RenderThread(RenderThread &&) = delete;
  auto operator=(RenderThread &&) -> RenderThread & = delete;

  auto Start(vulkan::Context *context, RenderData *data, FrameSubmissionPolicy policy) -> void;
  // Runs the production admission and worker loop with a CPU-only frame callback.
  auto StartForTest(FrameSubmissionPolicy policy,
    std::function<std::expected<void, Error>()> draw_frame,
    std::function<void()> wait_idle = {}) -> void;
  auto WaitForShutdownForTest() -> void;
  auto WaitForBlockedProducerCountForTest(size_t count) -> void;
  void Stop();

  [[nodiscard]] auto SubmitFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<void, Error>;
  [[nodiscard]] auto PollError() const -> std::optional<Error>;
  [[nodiscard]] auto CompletedFrameCount() const -> u64;
  [[nodiscard]] auto RecreatedFrameCount() const -> u64;
  [[nodiscard]] auto WaitIdle() -> std::expected<void, Error>;

private:
  enum class Command : u8 {
    kNone,
    kWaitIdle,
    kShutdown,
  };

  enum class FrameOutcome : u8 {
    kSubmitted,
    kSkipped,
    kSwapchainRecreated,
  };

  struct PendingFrame
  {
    f64 aspect_ratio{};
    CameraSnapshot camera{};
    ImGuiFrameSnapshot imgui{};
  };

  auto ThreadMain() -> void;
  auto ProcessWaitIdle(std::unique_lock<std::mutex> &lock) -> void;
  auto StartWorker(vulkan::Context *context,
    RenderData *data,
    FrameSubmissionPolicy policy,
    std::function<std::expected<void, Error>()> draw_frame,
    std::function<void()> wait_idle) -> void;
  auto FinishFrame(size_t frame_slot, std::expected<FrameOutcome, Error> const &drawn) -> void;
  [[nodiscard]] auto DrawFrameVulkan(size_t frame_slot) const -> std::expected<FrameOutcome, Error>;

  vulkan::Context *context_{ nullptr };
  RenderData *data_{ nullptr };
  std::jthread thread_;
  std::function<std::expected<void, Error>()> draw_frame_override_;
  std::function<void()> wait_idle_override_;

  mutable std::mutex control_mutex_;
  mutable std::condition_variable control_cv_;

  Command command_{ Command::kNone };
  adt::LatestFrameMailbox<PendingFrame, kFrameSlotCount> mailbox_;
  // Worker failures are terminal and stay visible to every caller until Start().
  std::optional<Error> first_error_;
  u64 completed_frame_count_{ 0U };
  u64 recreated_frame_count_{ 0U };
  bool worker_busy_{ false };
  bool thread_running_{ false };
  bool idle_done_{ true };
  size_t blocked_producer_count_{ 0U };
  bool test_mode_{ false };
  FrameSubmissionPolicy submission_policy_{ FrameSubmissionPolicy::kLatest };
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP
