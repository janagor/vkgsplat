// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include "../src/vkgsplat_capture/video_recorder_test_access.hpp"
#include "../src/vkgsplat_io/video/video_sink.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vkgsplat/camera.hpp>
#include <vkgsplat_capture/video_recorder.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vkgsplat_video/encoded_packet.hpp>
#include <vkgsplat_video/recording_session.hpp>

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <memory>
#include <span>
#include <system_error>
#include <utility>

namespace {

constexpr int kRecorderOwnedObjectCount = 2;

struct RecorderState
{
  int captures = 0;
  int session_finishes = 0;
  int writes = 0;
  int sink_finishes = 0;
  int destroyed = 0;
  bool capture_fails = false;
  bool session_finish_fails = false;
  bool write_fails = false;
  bool sink_finish_fails = false;
};

class FakeSession final : public vkgsplat::video::RecordingSession
{
public:
  explicit FakeSession(RecorderState &state) : state_{ state } {}
  ~FakeSession() override { ++state_.destroyed; }
  FakeSession(FakeSession const &) = delete;
  auto operator=(FakeSession const &) -> FakeSession & = delete;
  FakeSession(FakeSession &&) = delete;
  auto operator=(FakeSession &&) -> FakeSession & = delete;

  [[nodiscard]] auto CaptureFrame([[maybe_unused]] vkgsplat::Camera const &camera, vkgsplat::u64 index)
    -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error> override
  {
    ++state_.captures;
    if (state_.capture_fails) {
      return std::unexpected{ vkgsplat::MakeError(std::errc::io_error, "injected capture failure") };
    }
    return vkgsplat::EncodedPacket{ .frame_index = index };
  }

  [[nodiscard]] auto Finish() -> std::expected<void, vkgsplat::Error> override
  {
    ++state_.session_finishes;
    if (state_.session_finish_fails) {
      return std::unexpected{ vkgsplat::MakeError(std::errc::io_error, "injected session finish failure") };
    }
    return {};
  }

  [[nodiscard]] auto ParameterSets() const noexcept -> std::span<std::byte const> override { return {}; }

private:
  RecorderState &state_;
};

class FakeSink final : public vkgsplat::VideoSink
{
public:
  explicit FakeSink(RecorderState &state) : state_{ state } {}
  ~FakeSink() override { ++state_.destroyed; }
  FakeSink(FakeSink const &) = delete;
  auto operator=(FakeSink const &) -> FakeSink & = delete;
  FakeSink(FakeSink &&) = delete;
  auto operator=(FakeSink &&) -> FakeSink & = delete;

  auto Write([[maybe_unused]] vkgsplat::EncodedPacket const &packet) -> std::expected<void, vkgsplat::Error> override
  {
    ++state_.writes;
    if (state_.write_fails) {
      return std::unexpected{ vkgsplat::MakeError(std::errc::io_error, "injected sink write failure") };
    }
    return {};
  }

  auto Finish() -> std::expected<void, vkgsplat::Error> override
  {
    ++state_.sink_finishes;
    if (state_.sink_finish_fails) {
      return std::unexpected{ vkgsplat::MakeError(std::errc::io_error, "injected sink finish failure") };
    }
    return {};
  }

private:
  RecorderState &state_;
};

[[nodiscard]] auto MakeRecorder(RecorderState &state) -> vkgsplat::capture::VideoRecorder
{
  return vkgsplat::capture::VideoRecorderTestAccess::Create(
    std::make_unique<FakeSession>(state), std::make_unique<FakeSink>(state));
}

}// namespace

TEST_CASE("video recorder finishes once and rejects recording afterward", "[video]")
{
  RecorderState state;
  {
    auto recorder = MakeRecorder(state);
    vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
    REQUIRE(recorder.record_frame(camera));
    REQUIRE(recorder.finish());
    REQUIRE_FALSE(recorder.finish());
    REQUIRE_FALSE(recorder.record_frame(camera));
    REQUIRE(state.captures == 1);
    REQUIRE(state.session_finishes == 1);
    REQUIRE(state.writes == 1);
    REQUIRE(state.sink_finishes == 1);
  }
  REQUIRE(state.destroyed == 2);
}

TEST_CASE("empty video recorder failure is sticky", "[video]")
{
  RecorderState state;
  auto recorder = MakeRecorder(state);
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  auto empty = recorder.finish();
  REQUIRE_FALSE(empty);
  REQUIRE(empty.error().code() == std::errc::invalid_argument);
  REQUIRE_FALSE(recorder.record_frame(camera));
  REQUIRE_FALSE(recorder.finish());
  REQUIRE(state.captures == 0);
  REQUIRE(state.session_finishes == 0);
  REQUIRE(state.writes == 0);
  REQUIRE(state.sink_finishes == 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("capture and sink failures stop recorder retries", "[video]")
{
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  for (bool const capture_fails : { true, false }) {
    RecorderState state{ .capture_fails = capture_fails, .write_fails = !capture_fails };
    auto recorder = MakeRecorder(state);
    auto failed = recorder.record_frame(camera);
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error().code() == std::errc::io_error);
    REQUIRE_FALSE(recorder.record_frame(camera));
    REQUIRE_FALSE(recorder.finish());
    REQUIRE(state.captures == 1);
    REQUIRE(state.writes == (capture_fails ? 0 : 1));
    REQUIRE(state.session_finishes == 0);
  }
}

TEST_CASE("sink finalization failure is sticky", "[video]")
{
  RecorderState state{ .sink_finish_fails = true };
  auto recorder = MakeRecorder(state);
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(recorder.record_frame(camera));
  REQUIRE_FALSE(recorder.finish());
  REQUIRE_FALSE(recorder.finish());
  REQUIRE_FALSE(recorder.record_frame(camera));
  REQUIRE(state.captures == 1);
  REQUIRE(state.session_finishes == 1);
  REQUIRE(state.sink_finishes == 1);
}

TEST_CASE("session finalization failure prevents sink finalization and retries", "[video]")
{
  RecorderState state{ .session_finish_fails = true };
  auto recorder = MakeRecorder(state);
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  REQUIRE(recorder.record_frame(camera));
  auto failed = recorder.finish();
  REQUIRE_FALSE(failed);
  REQUIRE(failed.error().code() == std::errc::io_error);
  auto repeated = recorder.finish();
  REQUIRE_FALSE(repeated);
  REQUIRE(repeated.error().code() == failed.error().code());
  auto recorded = recorder.record_frame(camera);
  REQUIRE_FALSE(recorded);
  REQUIRE(recorded.error().code() == failed.error().code());
  REQUIRE(state.captures == 1);
  REQUIRE(state.session_finishes == 1);
  REQUIRE(state.sink_finishes == 0);
}

TEST_CASE("moving a recorder preserves its failure", "[video]")
{
  RecorderState state{ .capture_fails = true };
  auto source = MakeRecorder(state);
  vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
  auto failed = source.record_frame(camera);
  REQUIRE_FALSE(failed);
  auto recorder = std::move(source);

  REQUIRE(failed.error().code() == std::errc::io_error);
  auto repeated_record = recorder.record_frame(camera);
  REQUIRE_FALSE(repeated_record);
  REQUIRE(repeated_record.error().code() == failed.error().code());
  auto repeated = recorder.finish();
  REQUIRE_FALSE(repeated);
  REQUIRE(repeated.error().code() == failed.error().code());
  REQUIRE(state.captures == 1);
}

TEST_CASE("move assignment destroys the replaced recorder and transfers ownership", "[video]")
{
  RecorderState replaced;
  RecorderState transferred;
  {
    auto destination = MakeRecorder(replaced);
    auto source = MakeRecorder(transferred);
    vkgsplat::Camera const camera{ vkgsplat::CameraConfig{} };
    REQUIRE(destination.record_frame(camera));
    destination = std::move(source);
    REQUIRE(replaced.destroyed == kRecorderOwnedObjectCount);
    REQUIRE(replaced.captures == 1);
    REQUIRE(transferred.destroyed == 0);

    REQUIRE(destination.record_frame(camera));
    REQUIRE(destination.finish());
  }
  REQUIRE(replaced.destroyed == kRecorderOwnedObjectCount);
  REQUIRE(transferred.destroyed == kRecorderOwnedObjectCount);
  REQUIRE(transferred.session_finishes == 1);
  REQUIRE(transferred.sink_finishes == 1);
}
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
