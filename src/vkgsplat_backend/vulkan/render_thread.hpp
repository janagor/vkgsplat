#ifndef VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP
#define VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP

#include "app_state.hpp"
#include "gpu_timing_samples.hpp"
#include <vkgsplat_adt/latest_frame_mailbox.hpp>

#include <array>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <vkgsplat/camera.hpp>
#include <vkgsplat/gpu_timings.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat_backend/vkgsplat_backend_export.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_video/encoded_packet.hpp>

namespace vkgsplat {

namespace vulkan {
  struct Context;
  namespace video {
    class GpuColorConverter;
    class VideoEncoder;
  }
}
struct RenderData;

// Dedicated thread for Vulkan queue and command-buffer work.
// Presentation uses frame_ring; offscreen output uses a timeline semaphore.
class VKGSPLAT_BACKEND_EXPORT RenderThread
{
public:
  RenderThread() = default;
  ~RenderThread();

  RenderThread(RenderThread const &) = delete;
  auto operator=(RenderThread const &) -> RenderThread & = delete;
  RenderThread(RenderThread &&) = delete;
  auto operator=(RenderThread &&) -> RenderThread & = delete;

  auto Start(vulkan::Context *context,
    RenderData *data,
    FrameSubmissionPolicy policy,
    bool capture_gpu_timing_samples = false) -> void;
  // Runs the production admission and worker loop with a CPU-only frame callback.
  auto StartForTest(FrameSubmissionPolicy policy,
    std::function<std::expected<void, Error>()> draw_frame,
    std::function<void()> wait_idle = {},
    std::function<std::expected<RgbaFrame, Error>()> readback_frame = {},
    std::function<std::expected<EncodedPacket, Error>()> video_frame = {}) -> void;
  auto WaitForShutdownForTest() -> void;
  auto WaitForBlockedProducerCountForTest(size_t count) -> void;
  auto WaitForReadbackPendingForTest() -> void;
  auto WaitForCapturePendingForTest() -> void;
  auto WaitForVideoCapturePendingForTest() -> void;
  auto WaitForExclusivePendingForTest() -> void;
  void Stop();

  [[nodiscard]] auto SubmitFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<void, Error>;
  [[nodiscard]] auto CaptureFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<RgbaFrame, Error>;
  // A failed request retires the supplied encoder and converter. The caller
  // must keep both alive until this method returns, including during Stop().
  [[nodiscard]] auto CaptureVideoFrame(Camera const &camera, f64 aspect_ratio,
    vulkan::video::GpuColorConverter &converter, vulkan::video::VideoEncoder &encoder,
    u64 frame_index) -> std::expected<EncodedPacket, Error>;
  [[nodiscard]] auto CaptureVideoFrameForTest(Camera const &camera, f64 aspect_ratio,
    u64 frame_index) -> std::expected<EncodedPacket, Error>;
  [[nodiscard]] auto ReadLatestFrame() -> std::expected<RgbaFrame, Error>;
  // Drain queued frames, wait for GPU completion, then run without concurrent submission or readback.
  [[nodiscard]] auto RunExclusive(std::function<std::expected<void, Error>()> action) -> std::expected<void, Error>;
  // Call from an exclusive worker action when replacing the color target.
  auto InvalidateColorTarget() -> void;
  [[nodiscard]] auto LfdEmulateActive() const noexcept -> bool;
  [[nodiscard]] auto LfdEmulateCell() const noexcept -> std::array<u32, 2>;
  [[nodiscard]] auto PollError() const -> std::optional<Error>;
  [[nodiscard]] auto CompletedFrameCount() const -> u64;
  [[nodiscard]] auto RecreatedFrameCount() const -> u64;
  [[nodiscard]] auto WaitIdle() -> std::expected<void, Error>;
  [[nodiscard]] auto TakeGpuTimings() -> std::expected<std::vector<GpuFrameTimings>, Error>;
  [[nodiscard]] auto ReserveGpuTimingSamples(size_t count) -> std::expected<void, Error>;

private:
  enum class Command : u8 {
    kNone,
    kWaitIdle,
    kReadback,
    kExclusive,
    kShutdown,
  };

  enum class FrameOutcome : u8 {
    kSubmitted,
    kSkipped,
    kSwapchainRecreated,
  };

  struct CaptureRequest
  {
    std::optional<std::expected<RgbaFrame, Error>> result;
    bool completed{ false };
  };

  struct VideoCaptureRequest
  {
    vulkan::video::GpuColorConverter *converter = nullptr;
    vulkan::video::VideoEncoder *encoder = nullptr;
    u64 frame_index = 0U;
    std::optional<std::expected<EncodedPacket, Error>> result;
    bool completed{ false };
  };

  struct PendingFrame
  {
    std::shared_ptr<CaptureRequest> capture;
    std::shared_ptr<VideoCaptureRequest> video_capture;
    f64 aspect_ratio{};
    CameraSnapshot camera{};
    ImGuiFrameSnapshot imgui{};
  };

  auto ThreadMain() -> void;
  auto ProcessFrame(size_t frame_slot, PendingFrame const &frame) -> void;
  auto ProcessWaitIdle(std::unique_lock<std::mutex> &lock) -> void;
  auto ProcessReadback(std::unique_lock<std::mutex> &lock) -> void;
  auto ProcessExclusive(std::unique_lock<std::mutex> &lock) -> void;
  auto StartWorker(vulkan::Context *context,
    RenderData *data,
    FrameSubmissionPolicy policy,
    std::function<std::expected<void, Error>()> draw_frame,
    std::function<void()> wait_idle,
    std::function<std::expected<RgbaFrame, Error>()> readback_frame,
    std::function<std::expected<EncodedPacket, Error>()> video_frame) -> void;
  [[nodiscard]] auto CaptureVideoFrameImpl(Camera const &camera, f64 aspect_ratio,
    vulkan::video::GpuColorConverter *converter, vulkan::video::VideoEncoder *encoder,
    u64 frame_index) -> std::expected<EncodedPacket, Error>;
  auto FinishFrame(size_t frame_slot, std::expected<FrameOutcome, Error> const &drawn) -> void;
  [[nodiscard]] auto DrawFrameVulkan(size_t frame_slot) -> std::expected<FrameOutcome, Error>;
  [[nodiscard]] auto DrawFrameOffscreen(size_t frame_slot) -> std::expected<FrameOutcome, Error>;
  [[nodiscard]] auto ReadFrameOnWorker() -> std::expected<RgbaFrame, Error>;
  auto PublishGpuTiming(std::optional<GpuFrameTimings> sample) -> void;

  vulkan::Context *context_{ nullptr };
  RenderData *data_{ nullptr };
  std::jthread thread_;
  std::function<std::expected<void, Error>()> draw_frame_override_;
  std::function<void()> wait_idle_override_;
  std::function<std::expected<RgbaFrame, Error>()> readback_frame_override_;
  std::function<std::expected<EncodedPacket, Error>()> video_frame_override_;

  mutable std::mutex control_mutex_;
  mutable std::mutex operation_mutex_;
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
  bool readback_pending_{ false };
  bool exclusive_pending_{ false };
  bool color_target_valid_{ false };
  std::optional<std::expected<RgbaFrame, Error>> readback_result_;
  std::optional<std::expected<void, Error>> exclusive_result_;
  std::function<std::expected<void, Error>()> exclusive_action_;
  bool lfd_emulate_active_snapshot_{ false };
  std::array<u32, 2> lfd_emulate_cell_snapshot_{ 0U, 0U };
  size_t blocked_producer_count_{ 0U };
  bool test_mode_{ false };
  bool capture_gpu_timing_samples_{ false };
  GpuTimingSamples gpu_timing_samples_;
  FrameSubmissionPolicy submission_policy_{ FrameSubmissionPolicy::kLatest };
  std::vector<std::shared_ptr<CaptureRequest>> capture_requests_;
  std::vector<std::shared_ptr<VideoCaptureRequest>> video_capture_requests_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP
