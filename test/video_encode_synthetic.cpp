#include "../src/vkgsplat_backend/vulkan/video/video_encoder.hpp"
#include "../src/vkgsplat_backend/vulkan/vulkan_driver.hpp"
#include "../src/vkgsplat_io/video/h264_writer.hpp"
#include "video_encode_gpu_source.hpp"

#include <vkgsplat_headless/offscreen_platform.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <iostream>
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
constexpr std::uint8_t kFirstLuma = 64U;
constexpr std::int64_t kPacketDuration = 1;
constexpr std::uint64_t kGpuSourcePeriod = 2U;
constexpr std::string_view kUnsupportedDeviceMessage =
  "No rendering device supports required 8-bit 4:2:0 H.264 encoding and image formats.";

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
  std::uint8_t luma) -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error>
{
  if (index % kGpuSourcePeriod == 0U) {
    std::fill_n(cpu_frame.begin(), y_size, std::byte{ luma });
    return encoder.EncodeNv12(cpu_frame, index);
  }
  auto prepared = gpu_source.Prepare(luma);
  if (!prepared) { return std::unexpected(prepared.error()); }
  auto packet = encoder.EncodeImage(*prepared, index);
  if (packet) { gpu_source.Complete(); }
  return packet;
}

auto Run(std::filesystem::path const &output) -> int
{
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
  auto gpu_source = vkgsplat::test::GpuVideoSource::Create(context, device, kExtent);
  if (!gpu_source) {
    std::cerr << gpu_source.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto encoder = VideoEncoder::Create(context, std::move(device), kExtent);
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
  std::vector<std::byte> frame(y_size + y_size / kChromaSubsamplingDivisor);
  std::fill(frame.begin() + static_cast<std::ptrdiff_t>(y_size), frame.end(), std::byte{ kNeutralChroma });
  auto writer = vkgsplat::H264Writer::Open(output.string(), (*encoder)->ParameterSets());
  if (!writer) {
    std::cerr << "open H.264 output: " << writer.error().message() << '\n';
    return EXIT_FAILURE;
  }
  for (std::uint64_t index = 0U; index < kFrameCount; ++index) {
    auto const luma = static_cast<std::uint8_t>(kFirstLuma + index);
    auto packet = EncodeFrame(**encoder, **gpu_source, frame, y_size, index, luma);
    if (!packet) {
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
  return EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  constexpr int kExpectedArguments = 2;
  if (argc != kExpectedArguments) { return EXIT_FAILURE; }
  return Run(argv[1]);
}
