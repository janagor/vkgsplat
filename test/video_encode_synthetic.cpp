#include "../src/vkgsplat_backend/vulkan/video/video_encoder.hpp"
#include "../src/vkgsplat_backend/vulkan/video/color_converter.hpp"
#include "../src/vkgsplat_backend/vulkan/vulkan_driver.hpp"
#include "../src/vkgsplat_io/video/h264_writer.hpp"
#include "video_encode_gpu_source.hpp"
#include "video_encode_color_source.hpp"

#include <vkgsplat_headless/offscreen_platform.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using vkgsplat::vulkan::video::EncodeDevice;
using vkgsplat::vulkan::video::VideoEncoder;

constexpr int kSkipped = 77;
constexpr VkExtent2D kExtent{ .width = 1280, .height = 720 };
constexpr std::uint8_t kNeutralChroma = 128;
constexpr std::size_t kChromaSubsamplingDivisor = 2U;
constexpr std::uint64_t kFrameCount = 60U;
constexpr std::uint64_t kColorFrameCount = 2U;
constexpr std::uint64_t kFirstGpuColorFrameIndex = 0U;
constexpr std::uint64_t kNextFrameIncrement = 1U;
constexpr std::uint8_t kFirstLuma = 64U;
constexpr std::int64_t kPacketDuration = 1;
constexpr std::uint64_t kGpuSourcePeriod = 2U;
constexpr std::size_t kAsyncSlotCount = 3U;
constexpr std::size_t kConverterPoolSlotCount = 1U;
constexpr std::uint64_t kRejectedEncodeIndex = 1U;
constexpr std::int64_t kAsyncTimestampStep = 2;
constexpr std::string_view kUnsupportedDeviceMessage =
  "No rendering device supports required 8-bit 4:2:0 H.264 encoding and image formats.";

class ValidationErrorCounter
{
public:
  explicit ValidationErrorCounter(vkgsplat::vulkan::Context &context) : context_{ context }
  {
    if (!VKGSPLAT_TEST_VIDEO_VALIDATION) { return; }
    VkDebugUtilsMessengerCreateInfoEXT create{};
    create.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create.pfnUserCallback = &ValidationErrorCounter::Callback;
    create.pUserData = this;
    result_ = context_.inst_disp.createDebugUtilsMessengerEXT(&create, nullptr, &messenger_);
  }

  ~ValidationErrorCounter()
  {
    if (messenger_ != VK_NULL_HANDLE) { context_.inst_disp.destroyDebugUtilsMessengerEXT(messenger_, nullptr); }
  }

  ValidationErrorCounter(ValidationErrorCounter const &) = delete;
  auto operator=(ValidationErrorCounter const &) -> ValidationErrorCounter & = delete;
  [[nodiscard]] auto Ready() const noexcept -> bool
  { return result_ == VK_SUCCESS && (!VKGSPLAT_TEST_VIDEO_VALIDATION || messenger_ != VK_NULL_HANDLE); }
  [[nodiscard]] auto Count() const noexcept -> int { return errors_.load(); }

private:
  static VKAPI_ATTR auto VKAPI_CALL Callback(VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT, VkDebugUtilsMessengerCallbackDataEXT const *,
    void *user_data) -> VkBool32
  {
    auto *counter = static_cast<ValidationErrorCounter *>(user_data);
    ++counter->errors_;
    return VK_FALSE;
  }

  vkgsplat::vulkan::Context &context_;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  VkResult result_ = VK_SUCCESS;
  std::atomic<int> errors_{ 0 };
};

[[nodiscard]] auto RequireVideoEncode() -> bool
{
  auto const *value = std::getenv("VKGSPLAT_TEST_REQUIRE_VIDEO_ENCODE");
  return value != nullptr && std::string_view{ value } == "1";
}

auto PrintSelectionDetails(EncodeDevice const &device) -> void
{
  std::cout << "Device: " << device.capabilities.device_name << '\n';
  std::cout << "Capabilities: " << device.capabilities.diagnostic() << '\n';
  for (auto const &profile : device.capabilities.profiles) {
    std::cout << "H.264 profile " << static_cast<int>(profile.profile) << " source formats:";
    for (auto const &format : profile.source_formats) {
      std::cout << ' ' << static_cast<int>(format.format)
                << "(transfer destination=" << format.transfer_destination << ')';
    }
    std::cout << '\n';
  }
}

[[nodiscard]] auto EncodeFrame(VideoEncoder &encoder, vkgsplat::test::GpuVideoSource &gpu_source,
  std::vector<std::byte> &cpu_frame, std::size_t y_size, std::uint64_t index,
  std::uint8_t luma, bool gpu_only) -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error>
{
  if (!gpu_only && index % kGpuSourcePeriod == 0U) {
    std::fill_n(cpu_frame.begin(), y_size, std::byte{ luma });
    return encoder.EncodeNv12(cpu_frame, index);
  }
  auto prepared = gpu_source.Prepare(luma);
  if (!prepared) { return std::unexpected(prepared.error()); }
  auto packet = encoder.EncodeImage(*prepared, index);
  if (packet) { gpu_source.Complete(); }
  return packet;
}

auto Run(std::filesystem::path const &output, bool gpu_only, bool color_conversion,
  bool repeat_conversion, std::string_view failure_mode, bool asynchronous,
  bool asynchronous_color) -> int
{
  if (repeat_conversion || !failure_mode.empty()) {
    std::cout << "Vulkan validation checking "
              << (VKGSPLAT_TEST_VIDEO_VALIDATION ? "enabled" : "disabled; checking functional cleanup only")
              << '\n';
  }
  std::error_code remove_error;
  std::filesystem::remove(output, remove_error);
  if (remove_error) {
    std::cerr << "failed to clear previous H.264 output: " << remove_error.message() << '\n';
    return EXIT_FAILURE;
  }
  auto platform = vkgsplat::OffscreenPlatform::create({ .width = kExtent.width, .height = kExtent.height });
  if (!platform) { return EXIT_FAILURE; }
  auto driver = vkgsplat::vulkan::VulkanDriver::create(*platform,
    { .enable_validation = VKGSPLAT_TEST_VIDEO_VALIDATION,
      .video_encode = vkgsplat::VideoEncodeSupport::kRequired });
  if (!driver) {
    if (driver.error().code() == std::make_error_code(std::errc::function_not_supported)
        && std::string_view{ driver.error().message() }.starts_with(kUnsupportedDeviceMessage)
        && !RequireVideoEncode()) {
      std::cout << "SKIP: " << driver.error().message() << '\n';
      return kSkipped;
    }
    std::cerr << driver.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto &context = (*driver)->context();
  ValidationErrorCounter validation_errors{ context };
  if (!validation_errors.Ready()) {
    std::cerr << "failed to install Vulkan validation error counter\n";
    return EXIT_FAILURE;
  }
  if (!context.video_encode) { return EXIT_FAILURE; }
  auto device = *context.video_encode;
  auto selection = device.capabilities.select({
    .require_conversion = true,
    .require_references = false,
    .required_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
    .require_transfer_destination = true,
    .required_extent = kExtent,
  });
  if (!selection) {
    PrintSelectionDetails(device);
    std::cout << "1280x720 NV12 transfer destination unavailable: no conversion-capable selection\n";
    return RequireVideoEncode() ? EXIT_FAILURE : kSkipped;
  }
  if (selection->source_format.format != VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
      || selection->upload_strategy != vkgsplat::vulkan::video::UploadStrategy::kTransferDestination
      || kExtent.width > selection->max_extent.width || kExtent.height > selection->max_extent.height) {
    PrintSelectionDetails(device);
    std::cout << "1280x720 NV12 transfer destination unavailable: selected format "
              << static_cast<int>(selection->source_format.format)
              << ", upload strategy " << static_cast<int>(selection->upload_strategy)
              << ", maximum extent " << selection->max_extent.width << 'x'
              << selection->max_extent.height << '\n';
    return RequireVideoEncode() ? EXIT_FAILURE : kSkipped;
  }
  device.selection = *selection;
  auto async_device = device;
  if (color_conversion && !asynchronous_color) {
    auto const &components = device.selection.source_format.components;
    std::cout << "Color encode device: " << device.capabilities.device_name
              << ", driver version: " << device.capabilities.driver_version
              << ", NV12 component mapping: "
              << static_cast<int>(components.r) << ','
              << static_cast<int>(components.g) << ','
              << static_cast<int>(components.b) << ','
              << static_cast<int>(components.a) << '\n';
  }
  std::unique_ptr<vkgsplat::test::GpuVideoSource> gpu_source;
  std::unique_ptr<vkgsplat::test::GpuColorSource> color_source;
  std::unique_ptr<vkgsplat::vulkan::video::GpuColorConverter> converter;
  if (color_conversion && !asynchronous_color) {
    auto created_source = vkgsplat::test::GpuColorSource::Create(context, kExtent);
    if (!created_source) {
      std::cerr << created_source.error().message() << '\n';
      return EXIT_FAILURE;
    }
    color_source = std::move(*created_source);
    auto created_converter = vkgsplat::vulkan::video::GpuColorConverter::Create(context, device, kExtent);
    if (!created_converter) {
      std::cerr << created_converter.error().message() << '\n';
      return EXIT_FAILURE;
    }
    converter = std::move(*created_converter);
  } else if (!asynchronous_color) {
    auto created_source = vkgsplat::test::GpuVideoSource::Create(context, device, kExtent);
    if (!created_source) {
      std::cerr << created_source.error().message() << '\n';
      return EXIT_FAILURE;
    }
    gpu_source = std::move(*created_source);
  }
  constexpr std::uint32_t kFpsNumerator = 30U;
  constexpr std::uint32_t kFpsDenominator = 1U;
  auto encoder = VideoEncoder::Create(context, std::move(device), kExtent,
    kFpsNumerator, kFpsDenominator,
    asynchronous ? kAsyncSlotCount : VideoEncoder::kOfflineInFlightFrames,
    !asynchronous);
  if (!encoder) {
    if (encoder.error().code() == std::make_error_code(std::errc::function_not_supported)) {
      std::cout << "SKIP: " << encoder.error().message() << '\n';
      return kSkipped;
    }
    std::cerr << encoder.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto const y_size = static_cast<std::size_t>(kExtent.width) * kExtent.height;
  if ((*encoder)->EncodeImage({}, 0U)) {
    std::cerr << "encoder accepted an unprepared GPU image\n";
    return EXIT_FAILURE;
  }
  if (failure_mode == "--fault-converter-submit") { converter->FailNextSubmissionForTest(); }
  if (failure_mode == "--fault-video-submit") {
    (*encoder)->FailSubmissionForTest(vkgsplat::vulkan::video::EncodeFailurePointForTest::kVideoSubmission);
  }
  if (failure_mode == "--fault-readback-submit" || failure_mode == "--fault-readback-destructor") {
    (*encoder)->FailSubmissionForTest(vkgsplat::vulkan::video::EncodeFailurePointForTest::kReadbackSubmission);
  }
  std::vector<std::byte> frame(y_size + y_size / kChromaSubsamplingDivisor);
  std::fill(frame.begin() + static_cast<std::ptrdiff_t>(y_size), frame.end(), std::byte{ kNeutralChroma });
  auto writer = vkgsplat::H264Writer::Open(output.string(), (*encoder)->ParameterSets());
  if (!writer) {
    std::cerr << "open H.264 output: " << writer.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (asynchronous_color) {
    auto created_pool = vkgsplat::vulkan::video::GpuColorConverterPool::Create(
      context, async_device, kExtent, kConverterPoolSlotCount);
    if (!created_pool) { return EXIT_FAILURE; }
    auto pool = std::move(*created_pool);
    auto create_source = [&context]()
      -> std::expected<std::shared_ptr<vkgsplat::test::GpuColorSource>, vkgsplat::Error> {
      auto created = vkgsplat::test::GpuColorSource::Create(context, kExtent);
      if (!created) { return std::unexpected(created.error()); }
      return std::shared_ptr<vkgsplat::test::GpuColorSource>{ std::move(*created) };
    };
    auto rejected_source = create_source();
    if (!rejected_source || !(*rejected_source)->Prepare()) { return EXIT_FAILURE; }
    {
      auto converted = pool->Convert((*rejected_source)->Image(),
        (*rejected_source)->ReadyTimeline(), (*rejected_source)->ReadyValue(), *rejected_source);
      if (!converted) { return EXIT_FAILURE; }
      auto unavailable = pool->Convert((*rejected_source)->Image(),
        (*rejected_source)->ReadyTimeline(), (*rejected_source)->ReadyValue(), *rejected_source);
      if (unavailable || unavailable.error().code() != std::errc::resource_unavailable_try_again) {
        return EXIT_FAILURE;
      }
      auto rejected = (*encoder)->SubmitImage(converted->prepared,
        vkgsplat::vulkan::video::PictureMetadata{
          .encode_index = kRejectedEncodeIndex,
          .presentation_timestamp = 0,
          .duration = kPacketDuration,
        }, converted->lease);
      if (rejected || rejected.error().code() != std::errc::operation_not_permitted) {
        return EXIT_FAILURE;
      }
    }
    if (auto retired = pool->RetireReleased(); !retired) { return EXIT_FAILURE; }
    rejected_source->reset();
    auto accepted_source = create_source();
    if (!accepted_source || !(*accepted_source)->Prepare()) { return EXIT_FAILURE; }
    auto converted = pool->Convert((*accepted_source)->Image(),
      (*accepted_source)->ReadyTimeline(), (*accepted_source)->ReadyValue(), *accepted_source);
    if (!converted) { return EXIT_FAILURE; }
    auto submitted = (*encoder)->SubmitImage(converted->prepared,
      vkgsplat::vulkan::video::PictureMetadata{
        .encode_index = 0U,
        .presentation_timestamp = 0,
        .duration = kPacketDuration,
      }, converted->lease);
    if (!submitted) { return EXIT_FAILURE; }
    converted->lease->MarkEncodeSubmitted();
    converted->lease.reset();
    auto packet = (*encoder)->CompleteImage();
    if (!packet || packet->frame_index != 0U) { return EXIT_FAILURE; }
    if (auto retired = pool->RetireReleased(); !retired) { return EXIT_FAILURE; }
    if (auto written = writer->WriteFrame(packet->data); !written) { return EXIT_FAILURE; }
    if (auto finished = (*encoder)->Finish(); !finished) { return EXIT_FAILURE; }
    if (auto finished = writer->Finish(); !finished) { return EXIT_FAILURE; }
    return validation_errors.Count() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (asynchronous) {
    std::vector<std::shared_ptr<vkgsplat::test::GpuVideoSource>> sources;
    std::vector<vkgsplat::vulkan::video::PreparedVideoFrame> prepared_frames;
    sources.reserve(kAsyncSlotCount);
    prepared_frames.reserve(kAsyncSlotCount);
    for (std::size_t slot = 0U; slot < kAsyncSlotCount; ++slot) {
      auto created = vkgsplat::test::GpuVideoSource::Create(context, async_device, kExtent);
      if (!created) { return EXIT_FAILURE; }
      auto source = std::shared_ptr<vkgsplat::test::GpuVideoSource>{ std::move(*created) };
      auto prepared = source->Prepare(static_cast<std::uint8_t>(kFirstLuma + slot));
      if (!prepared) { return EXIT_FAILURE; }
      auto submitted = (*encoder)->SubmitImage(*prepared,
        vkgsplat::vulkan::video::PictureMetadata{
          .encode_index = slot,
          .presentation_timestamp = static_cast<std::int64_t>(slot) * kAsyncTimestampStep,
          .duration = kPacketDuration,
        }, source);
      if (!submitted) { return EXIT_FAILURE; }
      prepared_frames.push_back(*prepared);
      sources.push_back(std::move(source));
    }
    if ((*encoder)->EncodeImage({}, kAsyncSlotCount)) { return EXIT_FAILURE; }
    auto const exhausted = (*encoder)->SubmitImage(prepared_frames.front(),
      vkgsplat::vulkan::video::PictureMetadata{
        .encode_index = kAsyncSlotCount,
        .presentation_timestamp = static_cast<std::int64_t>(kAsyncSlotCount) * kAsyncTimestampStep,
      }, sources.front());
    if (exhausted || exhausted.error().code() != std::errc::resource_unavailable_try_again) {
      return EXIT_FAILURE;
    }
    for (std::size_t slot = 0U; slot < kAsyncSlotCount; ++slot) {
      auto ready = (*encoder)->TryCompleteImage();
      if (!ready) { return EXIT_FAILURE; }
      auto completed = ready->has_value()
                         ? std::expected<vkgsplat::EncodedPacket, vkgsplat::Error>{ std::move(**ready) }
                         : (*encoder)->CompleteImage();
      if (!completed) { return EXIT_FAILURE; }
      auto packet = std::move(*completed);
      if (packet.frame_index != slot || packet.pts != static_cast<std::int64_t>(slot) * kAsyncTimestampStep
          || packet.dts != packet.pts || packet.duration != kPacketDuration) { return EXIT_FAILURE; }
      sources.at(slot)->Complete();
      if (auto written = writer->WriteFrame(packet.data); !written) { return EXIT_FAILURE; }
    }
    if (auto finished = (*encoder)->Finish(); !finished) { return EXIT_FAILURE; }
    if (auto finished = writer->Finish(); !finished) { return EXIT_FAILURE; }
    return validation_errors.Count() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  auto const frame_count = color_conversion ? kColorFrameCount : kFrameCount;
  for (std::uint64_t index = 0U; index < frame_count; ++index) {
    auto const luma = static_cast<std::uint8_t>(kFirstLuma + index);
    auto packet = [&]() -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error> {
      if (!color_conversion) { return EncodeFrame(**encoder, *gpu_source, frame, y_size, index, luma, gpu_only); }
      if (index != kFirstGpuColorFrameIndex && !repeat_conversion) {
        return (*encoder)->EncodeNv12(frame, index);
      }
      if (index != kFirstGpuColorFrameIndex) {
        color_source.reset();
        auto created_source = vkgsplat::test::GpuColorSource::Create(context, kExtent);
        if (!created_source) { return std::unexpected(created_source.error()); }
        color_source = std::move(*created_source);
      }
      if (auto prepared = color_source->Prepare(); !prepared) { return std::unexpected(prepared.error()); }
      auto converted = converter->Convert(color_source->Image(),
        color_source->ReadyTimeline(), color_source->ReadyValue());
      if (!converted) { return std::unexpected(converted.error()); }
      if (auto released = converter->WaitForSourceRelease(); !released) {
        return std::unexpected(released.error());
      }
      auto const nv12_size = static_cast<VkDeviceSize>(y_size + y_size / kChromaSubsamplingDivisor);
      auto converted_bytes = color_source->ReadNv12Buffer(converter->Nv12BufferForTest(), nv12_size);
      if (!converted_bytes) { return std::unexpected(converted_bytes.error()); }
      frame = std::move(*converted_bytes);
      if (index == kFirstGpuColorFrameIndex) {
        std::ofstream nv12{ output.string() + ".nv12", std::ios::binary | std::ios::trunc };
        nv12.write(reinterpret_cast<char const *>(frame.data()),
          static_cast<std::streamsize>(frame.size()));
        if (!nv12) {
          return std::unexpected(vkgsplat::MakeError(std::errc::io_error,
            "write converted NV12 test output failed"));
        }
      }
      auto encoded = (*encoder)->EncodeImage(*converted, index);
      if (!encoded) { return encoded; }
      if (auto completed = converter->Complete(); !completed) { return std::unexpected(completed.error()); }
      return encoded;
    }();
    if (!packet) {
      if (!failure_mode.empty()) {
        auto const expected_message = failure_mode == "--fault-converter-submit"
                                        ? "injected conversion submission failure"
                                      : failure_mode == "--fault-video-submit"
                                        ? "injected video submission failure"
                                        : "injected bitstream readback submission failure";
        if (packet.error().message() != expected_message) {
          std::cerr << packet.error().message() << '\n';
          return EXIT_FAILURE;
        }
        if (packet.error().code() != std::errc::io_error) { return EXIT_FAILURE; }
        if (failure_mode != "--fault-readback-destructor") {
          if (auto drained = (*encoder)->WaitForInFlightWork(); !drained) {
            std::cerr << drained.error().message() << '\n';
            return EXIT_FAILURE;
          }
          if (auto released = converter->WaitForSourceRelease(); !released) {
            std::cerr << released.error().message() << '\n';
            return EXIT_FAILURE;
          }
        }
        (*encoder)->Abort();
        converter->Abort();
        auto retired_encode = (*encoder)->EncodeNv12(frame, index + kNextFrameIncrement);
        if (retired_encode || retired_encode.error().code() != std::errc::state_not_recoverable) {
          return EXIT_FAILURE;
        }
        auto retired_convert = converter->Convert(color_source->Image(),
          color_source->ReadyTimeline(), color_source->ReadyValue());
        if (retired_convert || retired_convert.error().code() != std::errc::state_not_recoverable) {
          return EXIT_FAILURE;
        }
        encoder->reset();
        converter.reset();
        color_source.reset();
        if (validation_errors.Count() != 0) {
          std::cerr << "Vulkan validation reported an error during failure cleanup\n";
          return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
      }
      if (packet.error().code() == std::make_error_code(std::errc::function_not_supported)) {
        std::cout << "SKIP: " << packet.error().message() << '\n';
        return kSkipped;
      }
      std::cerr << packet.error().message() << '\n';
      return EXIT_FAILURE;
    }
    if (packet->frame_index != index || packet->pts != static_cast<std::int64_t>(index)
        || packet->dts != packet->pts || packet->duration != kPacketDuration
        || !packet->keyframe || packet->data.empty()) {
      std::cerr << "invalid H.264 packet metadata at frame " << index << '\n';
      return EXIT_FAILURE;
    }
    if (auto written = writer->WriteFrame(packet->data); !written) {
      std::cerr << "write H.264 picture: " << written.error().message() << '\n';
      return EXIT_FAILURE;
    }
  }
  if (auto finished = (*encoder)->Finish(); !finished) {
    std::cerr << "finalize H.264 encoder: " << finished.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (auto finished = writer->Finish(); !finished) {
    std::cerr << "finalize H.264 output: " << finished.error().message() << '\n';
    return EXIT_FAILURE;
  }
  encoder->reset();
  converter.reset();
  color_source.reset();
  gpu_source.reset();
  if (validation_errors.Count() != 0) {
    std::cerr << "Vulkan validation reported an error during encode teardown\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  constexpr int kDefaultArguments = 2;
  constexpr int kModeArguments = 3;
  if (argc != kDefaultArguments && argc != kModeArguments) { return EXIT_FAILURE; }
  auto const mode = argc == kModeArguments ? std::string_view{ argv[2] } : std::string_view{};
  if (!mode.empty() && mode != "--gpu-only" && mode != "--color-conversion"
      && mode != "--async" && mode != "--async-color"
      && mode != "--repeat-color-conversion" && mode != "--fault-converter-submit"
      && mode != "--fault-video-submit" && mode != "--fault-readback-submit"
      && mode != "--fault-readback-destructor") { return EXIT_FAILURE; }
  auto const failure_mode = mode.starts_with("--fault-") ? mode : std::string_view{};
  return Run(argv[1], mode == "--gpu-only",
    mode == "--color-conversion" || mode == "--repeat-color-conversion" || !failure_mode.empty(),
    mode == "--repeat-color-conversion", failure_mode,
    mode == "--async" || mode == "--async-color", mode == "--async-color");
}
