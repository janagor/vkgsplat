#ifndef VKGSPLAT_CAPTURE_VIDEO_RECORDER_HPP
#define VKGSPLAT_CAPTURE_VIDEO_RECORDER_HPP

#include <vkgsplat_capture/video_config.hpp>
#include <vkgsplat_capture/vkgsplat_capture_export.hpp>

#include <expected>
#include <memory>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {
class Camera;
class Renderer;

namespace capture {

// Blocking offline recorder. The borrowed renderer must outlive the recorder.
class VKGSPLAT_CAPTURE_EXPORT VideoRecorder
{
public:
  [[nodiscard]] static auto create(Renderer &renderer, VideoConfig config)
    -> std::expected<VideoRecorder, Error>;
  [[nodiscard]] auto record_frame(Camera const &camera) -> std::expected<void, Error>;
  [[nodiscard]] auto finish() -> std::expected<void, Error>;

  VideoRecorder(VideoRecorder &&) noexcept;
  auto operator=(VideoRecorder &&) noexcept -> VideoRecorder &;
  VideoRecorder(VideoRecorder const &) = delete;
  auto operator=(VideoRecorder const &) -> VideoRecorder & = delete;
  ~VideoRecorder() noexcept;

private:
  struct Impl;
  explicit VideoRecorder(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}// namespace capture
}// namespace vkgsplat

#endif// VKGSPLAT_CAPTURE_VIDEO_RECORDER_HPP
