#include "h264_writer.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <system_error>

namespace vkgsplat {
namespace {

  constexpr std::array kStartCodeFour{
    std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }
  };
  constexpr std::array kStartCodeThree{
    std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }
  };

  [[nodiscard]] auto StartCodeSize(std::span<std::byte const> bytes) noexcept -> size_t
  {
    if (bytes.size() >= kStartCodeFour.size()
        && std::equal(kStartCodeFour.begin(), kStartCodeFour.end(), bytes.begin())) {
      return kStartCodeFour.size();
    }
    if (bytes.size() >= kStartCodeThree.size()
        && std::equal(kStartCodeThree.begin(), kStartCodeThree.end(), bytes.begin())) {
      return kStartCodeThree.size();
    }
    return 0;
  }

  struct NaluTypes
  {
    bool sps = false;
    bool pps = false;
    bool idr = false;
    bool picture = false;
  };

  [[nodiscard]] auto ScanAnnexB(std::span<std::byte const> bytes) -> std::expected<NaluTypes, Error>
  {
    constexpr std::uint8_t kNaluTypeMask = 0x1f;
    constexpr std::uint8_t kForbiddenZeroBit = 0x80;
    constexpr std::uint8_t kIdrNaluType = 5;
    constexpr std::uint8_t kNonIdrNaluType = 1;
    constexpr std::uint8_t kSpsNaluType = 7;
    constexpr std::uint8_t kPpsNaluType = 8;
    if (StartCodeSize(bytes) == 0) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "H.264 data lacks an Annex B start code"));
    }
    NaluTypes types;
    size_t offset = 0;
    while (offset < bytes.size()) {
      auto const prefix = StartCodeSize(bytes.subspan(offset));
      if (prefix == 0 || offset + prefix >= bytes.size()) {
        return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 Annex B NAL unit"));
      }
      auto const header = std::to_integer<std::uint8_t>(bytes[offset + prefix]);
      if ((header & kForbiddenZeroBit) != 0 || (header & kNaluTypeMask) == 0) {
        return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 NAL header"));
      }
      auto const type = header & kNaluTypeMask;
      types.sps |= type == kSpsNaluType;
      types.pps |= type == kPpsNaluType;
      types.idr |= type == kIdrNaluType;
      types.picture |= type == kIdrNaluType || type == kNonIdrNaluType;
      size_t next = bytes.size();
      for (size_t index = offset + prefix + 1; index < bytes.size(); ++index) {
        if (StartCodeSize(bytes.subspan(index)) != 0) {
          next = index;
          break;
        }
      }
      offset = next;
    }
    return types;
  }

  [[nodiscard]] auto WriteBytes(std::ofstream &stream, std::span<std::byte const> bytes)
    -> std::expected<void, Error>
  {
    if (bytes.size() > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
      return std::unexpected(MakeError(std::errc::value_too_large, "H.264 data is too large to write"));
    }
    stream.write(reinterpret_cast<char const *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) { return std::unexpected(MakeError(std::errc::io_error, "failed to write H.264 bitstream")); }
    return {};
  }

}// namespace

auto H264Writer::Open(std::string_view path, std::span<std::byte const> parameter_sets)
  -> std::expected<H264Writer, Error>
{
  auto const types = ScanAnnexB(parameter_sets);
  if (!types || !types->sps || !types->pps) {
    return std::unexpected(MakeError(std::errc::invalid_argument,
      "H.264 parameter data must contain Annex B SPS and PPS NAL units"));
  }
  std::ofstream stream{ std::string{ path }, std::ios::binary | std::ios::trunc };
  if (!stream) { return std::unexpected(MakeError(std::errc::io_error, "failed to open H.264 output")); }
  if (auto written = WriteBytes(stream, parameter_sets); !written) {
    return std::unexpected(written.error());
  }
  return H264Writer{ std::move(stream) };
}

auto H264Writer::WriteFrame(std::span<std::byte const> picture) -> std::expected<void, Error>
{
  if (finished_ || !stream_.is_open()) {
    return std::unexpected(MakeError(std::errc::bad_file_descriptor, "H.264 output is finalized"));
  }
  auto const types = ScanAnnexB(picture);
  if (!types || !types->picture || (!seen_idr_ && !types->idr)) {
    return std::unexpected(MakeError(std::errc::invalid_argument,
      "H.264 picture must contain an Annex B slice, beginning with IDR"));
  }
  if (auto result = WriteBytes(stream_, picture); !result) { return result; }
  seen_idr_ |= types->idr;
  return {};
}

auto H264Writer::Finish() -> std::expected<void, Error>
{
  if (finished_ || !stream_.is_open()) {
    return std::unexpected(MakeError(std::errc::bad_file_descriptor, "H.264 output is finalized"));
  }
  stream_.close();
  finished_ = true;
  if (!stream_) { return std::unexpected(MakeError(std::errc::io_error, "failed to finalize H.264 output")); }
  return {};
}

}// namespace vkgsplat
