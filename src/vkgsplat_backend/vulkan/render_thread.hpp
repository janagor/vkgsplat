#ifndef VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP
#define VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP

#include <vkgsplat_adt/bounded_queue.hpp>
#include "app_state.hpp"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <mutex>
#include <optional>
#include <thread>

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

namespace vulkan { struct Context; }
struct RenderData;

// Dedicated thread for all Vulkan queue / swapchain / command-buffer recording work.
// Main enqueues CPU-ready frame slots (ring depth = kFrameSlotCount); the render thread
// drains them while main prepares the next slot. GPU completion for a slot is separate
// (slot_gpu_fences) and only waited when that slot is reused for another submit.
class RenderThread
{
public:
  RenderThread() = default;
  ~RenderThread();

  RenderThread(RenderThread const &) = delete;
  auto operator=(RenderThread const &) -> RenderThread & = delete;
  RenderThread(RenderThread &&) = delete;
  auto operator=(RenderThread &&) -> RenderThread & = delete;

  void Start(vulkan::Context *context, RenderData *data);
  void Stop();

  [[nodiscard]] auto SubmitFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<void, Error>;
  void WaitIdle();

private:
  enum class Command : u8
  {
    kNone,
    kWaitIdle,
    kShutdown,
  };

  struct FrameCompletion
  {
    std::optional<Error> error;
  };

  using PendingQueue = adt::BoundedQueue<size_t, kFrameSlotCount>;
  using CompletionQueue = adt::BoundedQueue<FrameCompletion, PendingQueue::kCapacity>;

  void ThreadMain();
  [[nodiscard]] auto DrawFrameVulkan(size_t frame_slot) -> std::expected<void, Error>;
  [[nodiscard]] auto DrainCompletions() -> std::optional<Error>;

  void WaitSlotReady(size_t slot);
  void MarkSlotReady(size_t slot);
  void MarkAllSlotsReady();
  void EnqueueCompletion(FrameCompletion const &completion);

  vulkan::Context *context_{ nullptr };
  RenderData *data_{ nullptr };
  std::jthread thread_;

  PendingQueue pending_queue_;
  CompletionQueue completion_queue_;

  // CPU ownership of FrameSetup: ready after record+submit for that slot (not GPU done).
  mutable std::mutex slot_mutex_;
  mutable std::condition_variable slot_ready_cv_;
  std::array<bool, PendingQueue::kCapacity> slot_ready_{};

  mutable std::mutex control_mutex_;
  mutable std::condition_variable control_cv_;

  Command command_{ Command::kNone };
  bool thread_running_{ false };
  bool idle_done_{ true };

  size_t submit_serial_{ 0 };
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP
