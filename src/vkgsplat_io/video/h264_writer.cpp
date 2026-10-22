#include "h264_writer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {
namespace {

  constexpr std::array kStartCodeFour{
    std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }
  };
  constexpr std::array kStartCodeThree{
    std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }
  };
  constexpr std::uint32_t kMaxTemporaryPathAttempts = 64U;

  [[nodiscard]] auto TemporaryPathSequence() noexcept -> std::atomic<std::uint64_t> &
  {
    static std::atomic<std::uint64_t> sequence{ 0U };
    return sequence;
  }

  [[nodiscard]] auto StartCodeSize(std::span<std::byte const> bytes) noexcept -> std::size_t
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
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      auto const prefix = StartCodeSize(bytes.subspan(offset));
      if (prefix == 0 || offset + prefix >= bytes.size()) {
        return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 Annex B NAL unit"));
      }
      auto const header = std::to_integer<std::uint8_t>(bytes.subspan(offset + prefix).front());
      if ((header & kForbiddenZeroBit) != 0 || (header & kNaluTypeMask) == 0) {
        return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 NAL header"));
      }
      auto const type = static_cast<std::uint8_t>(header & kNaluTypeMask);
      types.sps |= type == kSpsNaluType;
      types.pps |= type == kPpsNaluType;
      types.idr |= type == kIdrNaluType;
      types.picture |= type == kIdrNaluType || type == kNonIdrNaluType;
      std::size_t next = bytes.size();
      for (std::size_t index = offset + prefix + 1; index < bytes.size(); ++index) {
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
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
      return std::unexpected(MakeError(std::errc::value_too_large, "H.264 data is too large to write"));
    }
    std::string buffer;
    buffer.reserve(bytes.size());
    std::ranges::transform(bytes, std::back_inserter(buffer), [](std::byte byte) -> char {
      return static_cast<char>(std::to_integer<unsigned char>(byte));
    });
    stream.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (!stream) { return std::unexpected(MakeError(std::errc::io_error, "failed to write H.264 bitstream")); }
    return {};
  }

}// namespace

H264Writer::~H264Writer()
{
  if (stream_.is_open()) { stream_.close(); }
  if (!temporary_path_.empty()) {
    std::error_code error;
    std::filesystem::remove(temporary_path_, error);
  }
}

H264Writer::H264Writer(H264Writer &&other) noexcept
  : stream_{ std::move(other.stream_) }, path_{ std::move(other.path_) },
    temporary_path_{ std::exchange(other.temporary_path_, {}) },
    finished_{ other.finished_ }, seen_idr_{ other.seen_idr_ }
{}

auto H264Writer::operator=(H264Writer &&other) noexcept -> H264Writer &
{
  if (this == &other) { return *this; }
  if (stream_.is_open()) { stream_.close(); }
  if (!temporary_path_.empty()) {
    std::error_code error;
    std::filesystem::remove(temporary_path_, error);
  }
  stream_ = std::move(other.stream_);
  path_ = std::move(other.path_);
  temporary_path_ = std::exchange(other.temporary_path_, {});
  finished_ = other.finished_;
  seen_idr_ = other.seen_idr_;
  return *this;
}

auto H264Writer::Open(std::string_view path, std::span<std::byte const> parameter_sets)
  -> std::expected<H264Writer, Error>
{
  auto const types = ScanAnnexB(parameter_sets);
  if (!types || !types->sps || !types->pps) {
    return std::unexpected(MakeError(std::errc::invalid_argument,
      "H.264 parameter data must contain Annex B SPS and PPS NAL units"));
  }
  if (path.empty()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "H.264 output path is empty"));
  }
  std::filesystem::path destination{ path };
  std::error_code path_error;
  if (std::filesystem::exists(destination, path_error)) {
    return std::unexpected(MakeError(std::errc::file_exists, "H.264 output already exists"));
  }
  if (path_error) { return std::unexpected(MakeError(path_error, "inspect H.264 output path failed")); }
  std::filesystem::path temporary_path;
  std::ofstream stream;
  for (std::uint32_t attempt = 0U; attempt < kMaxTemporaryPathAttempts; ++attempt) {
    auto candidate = destination;
    candidate += ".partial." + std::to_string(TemporaryPathSequence().fetch_add(1U));
    stream.open(candidate, std::ios::out | std::ios::binary | std::ios::noreplace);
    if (stream) {
      temporary_path = std::move(candidate);
      break;
    }
    stream.clear();
    std::error_code candidate_error;
    if (!std::filesystem::exists(candidate, candidate_error) || candidate_error) {
      return std::unexpected(MakeError(std::errc::io_error, "reserve H.264 temporary output failed"));
    }
  }
  if (temporary_path.empty()) {
    return std::unexpected(MakeError(std::errc::file_exists, "no free H.264 temporary output name"));
  }
  H264Writer writer{ std::move(stream), std::move(destination), std::move(temporary_path) };
  if (auto written = WriteBytes(writer.stream_, parameter_sets); !written) {
    return std::unexpected(written.error());
  }
  return writer;
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
  std::error_code error;
  std::filesystem::create_hard_link(temporary_path_, path_, error);
  if (error) { return std::unexpected(MakeError(error, "publish H.264 output failed")); }
  std::filesystem::remove(temporary_path_, error);
  if (!error) { temporary_path_.clear(); }
  return {};
}

}// namespace vkgsplat
