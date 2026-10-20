#include "../src/vkgsplat_backend/vulkan/video/video_session.hpp"
#include "../src/vkgsplat_backend/vulkan/vulkan_driver.hpp"

#include <vkgsplat_headless/offscreen_platform.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr int kSkipped = 77;
constexpr VkExtent2D kExtent{ .width = 1280, .height = 720 };
constexpr std::uint32_t kMacroblockSize = 16;
constexpr std::uint32_t kMaxGolombZeros = 31;
constexpr std::uint32_t kBitsPerByte = 8;
constexpr std::uint32_t kMaximumBitRead = 32;
constexpr std::uint32_t kMaximumBitIndex = kBitsPerByte - 1;
constexpr size_t kShortStartCodeSize = 3;
constexpr size_t kLongStartCodeSize = 4;
constexpr std::uint8_t kSpsNaluType = 7;
constexpr std::uint8_t kPpsNaluType = 8;
constexpr std::uint8_t kNaluTypeMask = 0x1f;
constexpr std::uint8_t kEmulationPreventionByte = 3;
constexpr std::uint32_t kHighProfileIdc = 100;
constexpr std::uint32_t kChromaFormat444 = 3;
constexpr std::uint32_t kScalingListCount420 = 8;
constexpr std::uint32_t kScalingListCount444 = 12;
constexpr std::uint32_t kSmallScalingListCount = 6;
constexpr std::uint32_t kSmallScalingListSize = 16;
constexpr std::uint32_t kLargeScalingListSize = 64;
constexpr std::uint32_t kInitialScalingValue = 8;
constexpr std::uint32_t kScaleModulus = 256;
constexpr std::uint32_t kCropUnit420 = 2;
constexpr std::uint32_t kSessionRepetitions = 3;
constexpr std::string_view kUnsupportedDeviceMessage =
  "No rendering device supports required 8-bit 4:2:0 H.264 encoding and image formats.";

class BitReader
{
public:
  explicit BitReader(std::vector<std::uint8_t> bytes) : bytes_{ std::move(bytes) } {}

  [[nodiscard]] auto Bits(std::uint32_t count) -> std::optional<std::uint32_t>
  {
    if (count > kMaximumBitRead || count > bytes_.size() * kBitsPerByte - position_) { return std::nullopt; }
    std::uint32_t result = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
      auto const byte = bytes_[position_ / kBitsPerByte];
      result = (result << 1) | ((byte >> (kMaximumBitIndex - position_ % kBitsPerByte)) & 1U);
      ++position_;
    }
    return result;
  }

  [[nodiscard]] auto UnsignedGolomb() -> std::optional<std::uint32_t>
  {
    std::uint32_t zeros = 0;
    while (true) {
      auto bit = Bits(1);
      if (!bit) { return std::nullopt; }
      if (*bit != 0) { break; }
      if (++zeros > kMaxGolombZeros) { return std::nullopt; }
    }
    auto suffix = Bits(zeros);
    if (!suffix) { return std::nullopt; }
    return ((1U << zeros) - 1U) + *suffix;
  }

  [[nodiscard]] auto SignedGolomb() -> std::optional<std::int32_t>
  {
    auto value = UnsignedGolomb();
    if (!value) { return std::nullopt; }
    auto const magnitude = static_cast<std::int32_t>((*value + 1U) / 2U);
    return (*value & 1U) != 0 ? magnitude : -magnitude;
  }

private:
  std::vector<std::uint8_t> bytes_;
  size_t position_ = 0;
};

[[nodiscard]] auto SkipScalingList(BitReader &reader, std::uint32_t length) -> bool
{
  std::int32_t last = kInitialScalingValue;
  std::int32_t next = kInitialScalingValue;
  for (std::uint32_t index = 0; index < length; ++index) {
    if (next != 0) {
      auto delta = reader.SignedGolomb();
      if (!delta) { return false; }
      next = (last + *delta + static_cast<std::int32_t>(kScaleModulus))
             % static_cast<std::int32_t>(kScaleModulus);
    }
    last = next == 0 ? last : next;
  }
  return true;
}

[[nodiscard]] auto StartCodeSize(std::span<std::byte const> bytes, size_t offset) -> size_t
{
  if (offset + kLongStartCodeSize <= bytes.size() && bytes[offset] == std::byte{ 0 }
      && bytes[offset + 1] == std::byte{ 0 } && bytes[offset + 2] == std::byte{ 0 }
      && bytes[offset + 3] == std::byte{ 1 }) { return kLongStartCodeSize; }
  if (offset + kShortStartCodeSize <= bytes.size() && bytes[offset] == std::byte{ 0 }
      && bytes[offset + 1] == std::byte{ 0 } && bytes[offset + 2] == std::byte{ 1 }) {
    return kShortStartCodeSize;
  }
  return 0;
}

[[nodiscard]] auto FindRbsp(std::span<std::byte const> bytes, std::uint8_t wanted_type)
  -> std::optional<BitReader>
{
  for (size_t offset = 0; offset < bytes.size(); ++offset) {
    auto const prefix = StartCodeSize(bytes, offset);
    if (prefix == 0 || offset + prefix >= bytes.size()) { continue; }
    auto const header = std::to_integer<std::uint8_t>(bytes[offset + prefix]);
    if ((header & kNaluTypeMask) != wanted_type) { continue; }
    std::vector<std::uint8_t> rbsp;
    std::uint32_t preceding_zeros = 0;
    for (size_t index = offset + prefix + 1; index < bytes.size(); ++index) {
      if (StartCodeSize(bytes, index) != 0) { break; }
      auto const value = std::to_integer<std::uint8_t>(bytes[index]);
      if (preceding_zeros >= 2 && value == kEmulationPreventionByte) {
        preceding_zeros = 0;
        continue;
      }
      rbsp.push_back(value);
      preceding_zeros = value == 0 ? preceding_zeros + 1 : 0;
    }
    return BitReader{ std::move(rbsp) };
  }
  return std::nullopt;
}

struct SpsInfo
{
  std::uint32_t profile = 0;
  std::uint32_t level = 0;
  std::uint32_t id = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

[[nodiscard]] auto ParseSps(BitReader &reader) -> std::optional<SpsInfo>
{
  auto profile = reader.Bits(8);
  auto constraints = reader.Bits(8);
  auto level = reader.Bits(8);
  auto id = reader.UnsignedGolomb();
  if (!profile || !constraints || !level || !id) { return std::nullopt; }
  if (*profile == kHighProfileIdc) {
    auto chroma = reader.UnsignedGolomb();
    if (!chroma) { return std::nullopt; }
    if (*chroma == kChromaFormat444 && !reader.Bits(1)) { return std::nullopt; }
    if (!reader.UnsignedGolomb() || !reader.UnsignedGolomb() || !reader.Bits(1)) { return std::nullopt; }
    auto scaling = reader.Bits(1);
    if (!scaling) { return std::nullopt; }
    if (*scaling != 0) {
      auto const count = *chroma == kChromaFormat444 ? kScalingListCount444 : kScalingListCount420;
      for (std::uint32_t index = 0; index < count; ++index) {
        auto present = reader.Bits(1);
        if (!present) { return std::nullopt; }
        if (*present != 0 && !SkipScalingList(reader,
              index < kSmallScalingListCount ? kSmallScalingListSize : kLargeScalingListSize)) {
          return std::nullopt;
        }
      }
    }
  }
  auto frame_bits = reader.UnsignedGolomb();
  auto poc_type = reader.UnsignedGolomb();
  if (!frame_bits || !poc_type || *poc_type != 0 || !reader.UnsignedGolomb()
      || !reader.UnsignedGolomb() || !reader.Bits(1)) { return std::nullopt; }
  auto width_mbs = reader.UnsignedGolomb();
  auto height_mbs = reader.UnsignedGolomb();
  auto frame_only = reader.Bits(1);
  if (!width_mbs || !height_mbs || !frame_only || *frame_only != 1 || !reader.Bits(1)) {
    return std::nullopt;
  }
  auto crop = reader.Bits(1);
  if (!crop) { return std::nullopt; }
  std::uint32_t left = 0;
  std::uint32_t right = 0;
  std::uint32_t top = 0;
  std::uint32_t bottom = 0;
  if (*crop != 0) {
    auto crop_left = reader.UnsignedGolomb();
    auto crop_right = reader.UnsignedGolomb();
    auto crop_top = reader.UnsignedGolomb();
    auto crop_bottom = reader.UnsignedGolomb();
    if (!crop_left || !crop_right || !crop_top || !crop_bottom) { return std::nullopt; }
    left = *crop_left;
    right = *crop_right;
    top = *crop_top;
    bottom = *crop_bottom;
  }
  auto const coded_width = (*width_mbs + 1) * kMacroblockSize;
  auto const coded_height = (*height_mbs + 1) * kMacroblockSize;
  auto const horizontal_crop = (left + right) * kCropUnit420;
  auto const vertical_crop = (top + bottom) * kCropUnit420;
  if (horizontal_crop >= coded_width || vertical_crop >= coded_height) { return std::nullopt; }
  return SpsInfo{ .profile = *profile, .level = *level, .id = *id,
    .width = coded_width - horizontal_crop,
    .height = coded_height - vertical_crop };
}

[[nodiscard]] auto LevelNumber(StdVideoH264LevelIdc level) -> std::uint32_t
{
  constexpr std::uint32_t kLevels[]{
    10, 11, 12, 13, 20, 21, 22, 30, 31, 32, 40, 41, 42, 50, 51, 52, 60, 61, 62,
  };
  auto const index = static_cast<size_t>(level);
  return index < std::size(kLevels) ? kLevels[index] : 0;
}

auto Run() -> int
{
  auto platform = vkgsplat::OffscreenPlatform::create({ .width = kExtent.width, .height = kExtent.height });
  if (!platform) { return EXIT_FAILURE; }
  auto driver = vkgsplat::vulkan::VulkanDriver::create(*platform,
    { .enable_validation = VKGSPLAT_TEST_VIDEO_VALIDATION,
      .video_encode = vkgsplat::VideoEncodeSupport::kRequired });
  if (!driver) {
    if (driver.error().code() == std::make_error_code(std::errc::function_not_supported)
        && std::string_view{ driver.error().message() }.starts_with(kUnsupportedDeviceMessage)) {
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
  if (!selection || selection->source_format.format != VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
      || selection->upload_strategy != vkgsplat::vulkan::video::UploadStrategy::kTransferDestination
      || kExtent.width > selection->max_extent.width || kExtent.height > selection->max_extent.height) {
    std::cout << "SKIP: 1280x720 NV12 transfer destination unavailable\n";
    return kSkipped;
  }
  device.selection = *selection;
  auto invalid = device;
  invalid.selection.profile_index = invalid.capabilities.profiles.size();
  if (vkgsplat::vulkan::video::VideoSession::Create(context, invalid, kExtent)) { return EXIT_FAILURE; }
  VkExtent2D const unsupported_extent{ .width = 0, .height = kExtent.height };
  if (vkgsplat::vulkan::video::VideoSession::Create(context, device, unsupported_extent)) {
    return EXIT_FAILURE;
  }
  for (std::uint32_t repetition = 0; repetition < kSessionRepetitions; ++repetition) {
    auto session = vkgsplat::vulkan::video::VideoSession::Create(context, device, kExtent);
    if (!session) {
      std::cerr << session.error().message() << '\n';
      return EXIT_FAILURE;
    }
    auto const &data = (*session)->parameter_sets();
    if (data.empty()) { return EXIT_FAILURE; }
    auto sps = FindRbsp(data, kSpsNaluType);
    auto pps = FindRbsp(data, kPpsNaluType);
    if (!sps || !pps) { return EXIT_FAILURE; }
    auto parsed = ParseSps(*sps);
    auto pps_id = pps->UnsignedGolomb();
    auto pps_sps_id = pps->UnsignedGolomb();
    if (!parsed || !pps_id || !pps_sps_id) { return EXIT_FAILURE; }
    auto const &profile = device.capabilities.profiles[selection->profile_index];
    if (parsed->profile != static_cast<std::uint32_t>(selection->profile)
        || parsed->level != LevelNumber(profile.h264.maxLevelIdc)
        || parsed->width != kExtent.width || parsed->height != kExtent.height
        || parsed->id != 0 || *pps_id != 0 || *pps_sps_id != 0) { return EXIT_FAILURE; }
  }
  return EXIT_SUCCESS;
}

}// namespace

auto main() -> int { return Run(); }
