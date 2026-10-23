// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include <vkgsplat/engine.hpp>
#include <vkgsplat/renderer.hpp>
#include <vkgsplat/video_capabilities.hpp>
#include <vkgsplat_capture/video_recorder.hpp>
#include <vkgsplat_headless/offscreen_platform.hpp>
#include <vkgsplat_utility/types.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr vkgsplat::u32 kTestExtent = 64U;
constexpr vkgsplat::u32 kMismatchedRecordingExtent = kTestExtent + 1U;
constexpr int kSkipped = 77;
constexpr int kExpectedArguments = 2;
constexpr std::size_t kRgbaChannels = 4U;
constexpr std::uint32_t kRenderFrames = 3U;

auto Run(char const *scene_path) -> int
{
  auto platform = vkgsplat::OffscreenPlatform::create({ .width = kTestExtent, .height = kTestExtent });
  if (!platform) { return EXIT_FAILURE; }

  std::vector<std::uint8_t> disabled_pixels;
  {
    auto disabled = vkgsplat::Engine::create({}, *platform);
    if (!disabled) {
      std::cout << "SKIP: rendering device unavailable: " << disabled.error().message() << '\n';
      return kSkipped;
    }
    if (disabled->video_encode_capabilities().h264_enabled) { return EXIT_FAILURE; }
    auto renderer = disabled->create_renderer({ .ply_path = scene_path,
      .enable_imgui = false,
      .submission_policy = vkgsplat::FrameSubmissionPolicy::kLossless });
    if (!renderer) {
      std::cerr << renderer.error().message() << '\n';
      return EXIT_FAILURE;
    }
    auto frame = renderer->render_frame(vkgsplat::Camera{});
    if (!frame) {
      std::cerr << frame.error().message() << '\n';
      return EXIT_FAILURE;
    }
    disabled_pixels = std::move(frame->pixels);
  }

  bool optional_enabled = false;
  {
    auto optional = vkgsplat::Engine::create({ .video_encode = vkgsplat::VideoEncodeSupport::kOptional }, *platform);
    if (!optional) {
      std::cerr << optional.error().message() << '\n';
      return EXIT_FAILURE;
    }
    optional_enabled |= optional->video_encode_capabilities().h264_enabled;
    if (optional_enabled
        && (optional->video_encode_capabilities().max_width == 0
            || optional->video_encode_capabilities().max_height == 0)) {
      return EXIT_FAILURE;
    }
    std::cout << optional->video_encode_capabilities().diagnostic << '\n';
    auto renderer = optional->create_renderer({ .ply_path = scene_path,
      .enable_imgui = false,
      .submission_policy = vkgsplat::FrameSubmissionPolicy::kLossless });
    if (!renderer) {
      std::cerr << renderer.error().message() << '\n';
      return EXIT_FAILURE;
    }
    if (optional_enabled) {
      auto output_path = std::filesystem::path{ scene_path };
      output_path += ".mismatched.h264";
      auto mismatched = vkgsplat::capture::VideoRecorder::create(*renderer,
        {
          .output_path = output_path,
          .width = kMismatchedRecordingExtent,
          .height = kTestExtent,
        });
      if (mismatched || mismatched.error().code() != std::make_error_code(std::errc::invalid_argument)) {
        std::cerr << "Recording accepted an extent different from the renderer\n";
        return EXIT_FAILURE;
      }
    }
    for (std::uint32_t index = 0; index < kRenderFrames; ++index) {
      auto frame = renderer->render_frame(vkgsplat::Camera{});
      if (!frame) {
        std::cerr << frame.error().message() << '\n';
        return EXIT_FAILURE;
      }
      if (frame->width != kTestExtent || frame->height != kTestExtent
          || frame->pixels.size() != static_cast<std::size_t>(kTestExtent) * kTestExtent * kRgbaChannels
          || frame->pixels != disabled_pixels) {
        return EXIT_FAILURE;
      }
    }
  }

  auto required = vkgsplat::Engine::create({ .video_encode = vkgsplat::VideoEncodeSupport::kRequired }, *platform);
  if (optional_enabled && !required) {
    std::cerr << required.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (!required) { std::cout << "Required H.264 unavailable: " << required.error().message() << '\n'; }
  if (required && !required->video_encode_capabilities().h264_enabled) { return EXIT_FAILURE; }
  return EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  if (argc != kExpectedArguments) { return EXIT_FAILURE; }
  return Run(std::span{ argv, static_cast<std::size_t>(argc) }.subspan(1U).front());
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
