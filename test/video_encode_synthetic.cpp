#include "../src/vkgsplat_backend/vulkan/video/video_encoder.hpp"
#include "../src/vkgsplat_backend/vulkan/video/color_converter.hpp"
#include "../src/vkgsplat_backend/vulkan/vulkan_driver.hpp"
#include "../src/vkgsplat_io/video/h264_writer.hpp"
#include "video_encode_gpu_source.hpp"
#include "video_encode_color_source.hpp"

#include <vkgsplat_headless/offscreen_platform.hpp>

#include <algorithm>
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
constexpr std::uint64_t kGpuColorFrameIndex = 0U;
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

auto Run(std::filesystem::path const &output, bool gpu_only, bool color_conversion) -> int
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
  if (color_conversion) {
    auto const &components = device.selection.source_format.components;
    std::cout << "Color encode device: " << device.capabilities.device_name
              << ", NV12 component mapping: "
              << static_cast<int>(components.r) << ','
              << static_cast<int>(components.g) << ','
              << static_cast<int>(components.b) << ','
              << static_cast<int>(components.a) << '\n';
  }
  std::unique_ptr<vkgsplat::test::GpuVideoSource> gpu_source;
  std::unique_ptr<vkgsplat::test::GpuColorSource> color_source;
  std::unique_ptr<vkgsplat::vulkan::video::GpuColorConverter> converter;
  if (color_conversion) {
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
  } else {
    auto created_source = vkgsplat::test::GpuVideoSource::Create(context, device, kExtent);
    if (!created_source) {
      std::cerr << created_source.error().message() << '\n';
      return EXIT_FAILURE;
    }
    gpu_source = std::move(*created_source);
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
  auto const frame_count = color_conversion ? kColorFrameCount : kFrameCount;
  for (std::uint64_t index = 0U; index < frame_count; ++index) {
    auto const luma = static_cast<std::uint8_t>(kFirstLuma + index);
    auto packet = [&]() -> std::expected<vkgsplat::EncodedPacket, vkgsplat::Error> {
      if (!color_conversion) { return EncodeFrame(**encoder, *gpu_source, frame, y_size, index, luma, gpu_only); }
      if (index != kGpuColorFrameIndex) { return (*encoder)->EncodeNv12(frame, index); }
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
      std::ofstream nv12{ output.string() + ".nv12", std::ios::binary | std::ios::trunc };
      nv12.write(reinterpret_cast<char const *>(frame.data()),
        static_cast<std::streamsize>(frame.size()));
      if (!nv12) {
        return std::unexpected(vkgsplat::MakeError(std::errc::io_error,
          "write converted NV12 test output failed"));
      }
      auto encoded = (*encoder)->EncodeImage(*converted, index);
      if (!encoded) { return encoded; }
      if (auto completed = converter->Complete(); !completed) { return std::unexpected(completed.error()); }
      return encoded;
    }();
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
  constexpr int kDefaultArguments = 2;
  constexpr int kModeArguments = 3;
  if (argc != kDefaultArguments && argc != kModeArguments) { return EXIT_FAILURE; }
  auto const mode = argc == kModeArguments ? std::string_view{ argv[2] } : std::string_view{};
  if (!mode.empty() && mode != "--gpu-only" && mode != "--color-conversion") { return EXIT_FAILURE; }
  return Run(argv[1], mode == "--gpu-only", mode == "--color-conversion");
}
