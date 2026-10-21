#ifndef VKGSPLAT_CAPTURE_VIDEO_RECORDER_TEST_ACCESS_HPP
#define VKGSPLAT_CAPTURE_VIDEO_RECORDER_TEST_ACCESS_HPP

#include <vkgsplat_capture/video_recorder.hpp>

#include <memory>

namespace vkgsplat {
class VideoSink;
namespace video { class RecordingSession; }
}

namespace vkgsplat::capture {

struct VKGSPLAT_CAPTURE_EXPORT VideoRecorderTestAccess
{
  [[nodiscard]] static auto Create(std::unique_ptr<video::RecordingSession> session,
    std::unique_ptr<VideoSink> sink) -> VideoRecorder;
};

}// namespace vkgsplat::capture

#endif// VKGSPLAT_CAPTURE_VIDEO_RECORDER_TEST_ACCESS_HPP
