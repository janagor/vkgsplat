#ifndef VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP
#define VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP

#include <condition_variable>
#include <expected>
#include <mutex>
#include <optional>
#include <thread>

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

struct Init;
struct RenderData;

// Dedicated thread for all Vulkan queue / swapchain / command-buffer recording work.
class RenderThread
{
public:
  RenderThread() = default;
  ~RenderThread();

  RenderThread(RenderThread const &) = delete;
  auto operator=(RenderThread const &) -> RenderThread & = delete;
  RenderThread(RenderThread &&) = delete;
  auto operator=(RenderThread &&) -> RenderThread & = delete;

  void Start(Init *init, RenderData *data);
  void Stop();

  [[nodiscard]] auto SubmitFrame(Camera const &camera, f64 aspect_ratio) -> std::expected<void, Error>;
  void WaitIdle();

private:
  enum class Command : u8
  {
    kNone,
    kDraw,
    kWaitIdle,
    kShutdown,
  };

  void ThreadMain();
  [[nodiscard]] auto DrawFrameVulkan() -> std::expected<void, Error>;

  Init *init_{ nullptr };
  RenderData *data_{ nullptr };
  std::jthread thread_;

  mutable std::mutex mutex_;
  mutable std::condition_variable work_cv_;
  mutable std::condition_variable done_cv_;

  Command command_{ Command::kNone };
  bool thread_running_{ false };
  bool work_done_{ true };
  mutable std::optional<Error> work_error_;
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_RENDER_THREAD_HPP
