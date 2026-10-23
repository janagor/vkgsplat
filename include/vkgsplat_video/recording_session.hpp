#ifndef VKGSPLAT_VIDEO_RECORDING_SESSION_HPP
#define VKGSPLAT_VIDEO_RECORDING_SESSION_HPP

#include <vkgsplat_video/encoded_packet.hpp>

#include <cstddef>
#include <expected>
#include <span>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {
class Camera;

namespace video {

  // Internal bridge owned by the capture module; its implementation owns the
  // Vulkan encoder and converter and borrows the renderer.
  class RecordingSession
  {
  public:
    RecordingSession() = default;
    virtual ~RecordingSession() = default;
    RecordingSession(RecordingSession const &) = delete;
    auto operator=(RecordingSession const &) -> RecordingSession & = delete;
    RecordingSession(RecordingSession &&) = delete;
    auto operator=(RecordingSession &&) -> RecordingSession & = delete;
    [[nodiscard]] virtual auto CaptureFrame(Camera const &camera, u64 frame_index)
      -> std::expected<EncodedPacket, Error> = 0;
    [[nodiscard]] virtual auto Finish() -> std::expected<void, Error> = 0;
    [[nodiscard]] virtual auto ParameterSets() const noexcept -> std::span<std::byte const> = 0;
  };

}// namespace video
}// namespace vkgsplat

#endif// VKGSPLAT_VIDEO_RECORDING_SESSION_HPP
