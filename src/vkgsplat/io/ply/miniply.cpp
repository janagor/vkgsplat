/*
MIT License

Copyright (c) 2019 Vilya Harvey

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Vendored third-party code from miniply:
//   https://github.com/vilya/miniply

#include "io/ply/miniply.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace miniply {

namespace {

  //
  // PLY constants
  //

  constexpr uint32_t kPLYReadBufferSize = 128U * 1024U;
  constexpr uint32_t kPLYTempBufferSize = kPLYReadBufferSize;
  constexpr size_t kVerticesPerTriangle = 3U;
  constexpr size_t kIndicesPerTriangle = 3U;
  constexpr size_t kQuadrilateralVertices = 4U;
  constexpr size_t kQuadrilateralIndices = 6U;
  constexpr size_t kScalarValueBytes = 8U;
  constexpr size_t kDefaultTriIndexReserve = 64U;
  constexpr size_t kPropertyReserve = 10U;
  constexpr size_t kElementReserve = 4U;
  constexpr int kMaxIntDigits = 10;
  constexpr int kAsciiControlMax = 32;
  constexpr int kAsciiDelete = 127;
  constexpr unsigned kAsciiToLowerBit = 32U;
  constexpr float kReflexAngle = 10000.0F;
  constexpr int kDecimalRadix = 10;
  constexpr double kDecimalBase = static_cast<double>(kDecimalRadix);
  constexpr double kFractionScale = 0.1;
  constexpr uint32_t kEndianSwapHalf32 = 16U;
  constexpr uint32_t kEndianMask8In32 = 0xFF00FF00U;
  constexpr uint32_t kEndianMask8Low32 = 0x00FF00FFU;
  constexpr uint64_t kEndianSwapHalf64 = 32ULL;
  constexpr uint64_t kEndianSwapQuarter64 = 16ULL;
  constexpr uint64_t kEndianMask16In64 = 0xFFFF0000FFFF0000ULL;
  constexpr uint64_t kEndianMask16Low64 = 0x0000FFFF0000FFFFULL;
  constexpr uint64_t kEndianMask8In64 = 0xFF00FF00FF00FF00ULL;
  constexpr uint64_t kEndianMask8Low64 = 0x00FF00FF00FF00FFULL;
  constexpr size_t kFloatBytes = 4U;
  constexpr size_t kDoubleBytes = 8U;
  constexpr size_t kInt32Bytes = 4U;
  constexpr unsigned kBitsPerByte = 8U;
  constexpr unsigned kSignedPairToggle = 1U;

  constexpr std::array<std::string_view, 3> kPLYFileTypes = {
    "ascii",
    "binary_little_endian",
    "binary_big_endian",
  };

  constexpr std::array<uint32_t, 8> kPLYPropertySize = { 1, 1, 2, 2, 4, 4, 4, 8 };

  struct PLYTypeAlias
  {
    std::string_view name;
    PLYPropertyType type;
  };

  constexpr std::array<PLYTypeAlias, 17> kTypeAliases = { {
    { .name = "char", .type = PLYPropertyType::kChar },
    { .name = "uchar", .type = PLYPropertyType::kUChar },
    { .name = "short", .type = PLYPropertyType::kShort },
    { .name = "ushort", .type = PLYPropertyType::kUShort },
    { .name = "int", .type = PLYPropertyType::kInt },
    { .name = "uint", .type = PLYPropertyType::kUInt },
    { .name = "float", .type = PLYPropertyType::kFloat },
    { .name = "float32", .type = PLYPropertyType::kFloat },
    { .name = "float64", .type = PLYPropertyType::kDouble },
    { .name = "double", .type = PLYPropertyType::kDouble },
    { .name = "uint8", .type = PLYPropertyType::kUChar },
    { .name = "uint16", .type = PLYPropertyType::kUShort },
    { .name = "uint32", .type = PLYPropertyType::kUInt },
    { .name = "int8", .type = PLYPropertyType::kChar },
    { .name = "int16", .type = PLYPropertyType::kShort },
    { .name = "int32", .type = PLYPropertyType::kInt },
    { .name = "", .type = PLYPropertyType::kNone },
  } };

  constexpr float kPi = std::numbers::pi_v<float>;

  struct Vec2
  {
    float x;
    float y;
  };

  struct Vec3
  {
    float x;
    float y;
    float z;
  };

  [[nodiscard]] constexpr auto DoubleDigit(char character) -> double { return static_cast<double>(character - '0'); }

  [[nodiscard]] auto operator-(Vec2 lhs, Vec2 rhs) -> Vec2 { return Vec2{ .x = lhs.x - rhs.x, .y = lhs.y - rhs.y }; }

  [[nodiscard]] auto Dot(Vec2 lhs, Vec2 rhs) -> float { return (lhs.x * rhs.x) + (lhs.y * rhs.y); }

  [[nodiscard]] auto Length(Vec2 vec) -> float { return std::sqrt(Dot(vec, vec)); }

  [[nodiscard]] auto Normalize(Vec2 vec) -> Vec2
  {
    float const len = Length(vec);
    return Vec2{ .x = vec.x / len, .y = vec.y / len };
  }

  [[nodiscard]] auto operator-(Vec3 lhs, Vec3 rhs) -> Vec3
  { return Vec3{ .x = lhs.x - rhs.x, .y = lhs.y - rhs.y, .z = lhs.z - rhs.z }; }

  [[nodiscard]] auto Dot(Vec3 lhs, Vec3 rhs) -> float { return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z); }

  [[nodiscard]] auto Length(Vec3 vec) -> float { return std::sqrt(Dot(vec, vec)); }

  [[nodiscard]] auto Normalize(Vec3 vec) -> Vec3
  {
    float const len = Length(vec);
    return Vec3{ .x = vec.x / len, .y = vec.y / len, .z = vec.z / len };
  }

  [[nodiscard]] auto Cross(Vec3 lhs, Vec3 rhs) -> Vec3
  {
    return Vec3{ .x = (lhs.y * rhs.z) - (lhs.z * rhs.y),
      .y = (lhs.z * rhs.x) - (lhs.x * rhs.z),
      .z = (lhs.x * rhs.y) - (lhs.y * rhs.x) };
  }

  [[nodiscard]] auto IsWhitespace(char character) -> bool
  { return character == ' ' || character == '\t' || character == '\r'; }

  [[nodiscard]] auto IsDigit(char character) -> bool { return character >= '0' && character <= '9'; }

  [[nodiscard]] auto ToLowerAscii(char character) -> char
  { return static_cast<char>(static_cast<unsigned char>(character) | kAsciiToLowerBit); }


  [[nodiscard]] auto SpanChar(std::span<const char> buffer, size_t index) -> char
  { return buffer.subspan(index, 1U).front(); }

  template<typename T> [[nodiscard]] auto SpanAt(std::span<T> span, size_t index) -> T
  { return span.subspan(index, 1U).front(); }

  template<typename T> [[nodiscard]] auto SpanRef(std::span<T> span, size_t index) -> T &
  { return *std::next(span.begin(), static_cast<std::ptrdiff_t>(index)); }

  template<typename T> [[nodiscard]] auto SpanAt(std::span<const T> span, size_t index) -> T
  { return span.subspan(index, 1U).front(); }

  using FileHandle = std::unique_ptr<FILE, decltype(&fclose)>;

  [[nodiscard]] auto ByteSpan(void *dest, size_t size) -> std::span<std::byte>
  { return { static_cast<std::byte *>(dest), size }; }

  [[nodiscard]] auto IsLetter(char character) -> bool
  {
    char const lower = ToLowerAscii(character);
    return lower >= 'a' && lower <= 'z';
  }

  [[nodiscard]] auto IsAlnum(char character) -> bool { return IsDigit(character) || IsLetter(character); }

  [[nodiscard]] auto IsKeywordStart(char character) -> bool { return IsLetter(character) || character == '_'; }

  [[nodiscard]] auto IsKeywordPart(char character) -> bool { return IsAlnum(character) || character == '_'; }

  [[nodiscard]] auto IsSafeBufferEnd(char character) -> bool
  { return (character > 0 && character <= kAsciiControlMax) || (character >= kAsciiDelete); }

  [[nodiscard]] auto OpenFile(const char *filename, const char *mode) -> FileHandle
  {
#ifdef _WIN32
    FILE *file = nullptr;
    if (fopen_s(&file, filename, mode) != 0) { return { nullptr, &fclose }; }
    return { file, &fclose };
#else
    FileHandle handle{ fopen(filename, mode), &fclose };
    if (handle == nullptr) { return { nullptr, &fclose }; }
    return handle;
#endif
  }

  [[nodiscard]] auto FileSeek(FILE *file, int64_t offset, int origin) -> bool
  {
#ifdef _WIN32
    return _fseeki64(file, offset, origin) == 0;
#else
    return std::fseek(file, offset, origin) == 0;
#endif
  }

  template<typename T> [[nodiscard]] auto ReadValue(std::span<const std::byte> bytes) -> T
  {
    std::array<std::byte, sizeof(T)> storage{};
    std::memcpy(storage.data(), bytes.data(), sizeof(T));
    return std::bit_cast<T>(storage);
  }

  template<typename T> void WriteValue(std::span<std::byte> bytes, T value)
  {
    auto const storage = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    std::memcpy(bytes.data(), storage.data(), sizeof(T));
  }

  void EndianSwap2(std::span<std::byte, 2> data)
  {
    auto tmp = ReadValue<uint16_t>(data);
    auto const unsigned_tmp = static_cast<uint16_t>(tmp);
    tmp = static_cast<uint16_t>(
      static_cast<uint16_t>(unsigned_tmp >> kBitsPerByte) | static_cast<uint16_t>(unsigned_tmp << kBitsPerByte));
    WriteValue(data, tmp);
  }

  void EndianSwap4(std::span<std::byte, 4> data)
  {
    auto tmp = ReadValue<uint32_t>(data);
    tmp = (tmp >> kEndianSwapHalf32) | (tmp << kEndianSwapHalf32);
    tmp = ((tmp & kEndianMask8In32) >> kBitsPerByte) | ((tmp & kEndianMask8Low32) << kBitsPerByte);
    WriteValue(data, tmp);
  }

  void EndianSwap8(std::span<std::byte, kScalarValueBytes> data)
  {
    auto tmp = ReadValue<uint64_t>(data);
    tmp = (tmp >> kEndianSwapHalf64) | (tmp << kEndianSwapHalf64);
    tmp = ((tmp & kEndianMask16In64) >> kEndianSwapQuarter64) | ((tmp & kEndianMask16Low64) << kEndianSwapQuarter64);
    tmp = ((tmp & kEndianMask8In64) >> kBitsPerByte) | ((tmp & kEndianMask8Low64) << kBitsPerByte);
    WriteValue(data, tmp);
  }

  void EndianSwap(std::span<std::byte> data, PLYPropertyType type)
  {
    switch (kPLYPropertySize.at(static_cast<size_t>(type))) {
    case 2U:
      EndianSwap2(data.subspan<0, 2>());
      break;
    case 4U:
      EndianSwap4(data.subspan<0, 4>());
      break;
    case kScalarValueBytes:
      EndianSwap8(data.subspan<0, kScalarValueBytes>());
      break;
    default:
      break;
    }
  }

  void EndianSwapArray(std::span<std::byte> data, PLYPropertyType type, int count)
  {
    size_t const element_size = kPLYPropertySize.at(static_cast<size_t>(type));
    for (int index = 0; index < count; ++index) {
      EndianSwap(data.subspan(static_cast<size_t>(index) * element_size, element_size), type);
    }
  }

  template<class T> void CopyAndConvertTo(T *dest, std::span<const std::byte> src, PLYPropertyType src_type)
  {
    switch (src_type) {
    case PLYPropertyType::kChar:
      *dest = static_cast<T>(static_cast<unsigned char>(ReadValue<int8_t>(src)));
      break;
    case PLYPropertyType::kUChar:
      *dest = static_cast<T>(std::to_integer<uint8_t>(src.front()));
      break;
    case PLYPropertyType::kShort:
      *dest = static_cast<T>(ReadValue<int16_t>(src));
      break;
    case PLYPropertyType::kUShort:
      *dest = static_cast<T>(ReadValue<uint16_t>(src));
      break;
    case PLYPropertyType::kInt:
      *dest = static_cast<T>(ReadValue<int32_t>(src));
      break;
    case PLYPropertyType::kUInt:
      *dest = static_cast<T>(ReadValue<uint32_t>(src));
      break;
    case PLYPropertyType::kFloat:
      *dest = static_cast<T>(ReadValue<float>(src));
      break;
    case PLYPropertyType::kDouble:
      *dest = static_cast<T>(ReadValue<double>(src));
      break;
    case PLYPropertyType::kNone:
      break;
    }
  }

  void CopyAndConvert(std::span<std::byte> dest,
    PLYPropertyType dest_type,
    std::span<const std::byte> src,
    PLYPropertyType src_type)
  {
    switch (dest_type) {
    case PLYPropertyType::kChar: {
      int8_t value = 0;
      CopyAndConvertTo(&value, src, src_type);
      WriteValue(dest.subspan(0, 1), value);
      break;
    }
    case PLYPropertyType::kUChar: {
      uint8_t value = 0;
      CopyAndConvertTo(&value, src, src_type);
      dest.front() = std::byte{ value };
      break;
    }
    case PLYPropertyType::kShort: {
      int16_t value = 0;
      CopyAndConvertTo(&value, src, src_type);
      WriteValue(dest.subspan(0, 2), value);
      break;
    }
    case PLYPropertyType::kUShort: {
      uint16_t value = 0;
      CopyAndConvertTo(&value, src, src_type);
      WriteValue(dest.subspan(0, 2), value);
      break;
    }
    case PLYPropertyType::kInt: {
      int32_t value = 0;
      CopyAndConvertTo(&value, src, src_type);
      WriteValue(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::kUInt: {
      uint32_t value = 0;
      CopyAndConvertTo(&value, src, src_type);
      WriteValue(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::kFloat: {
      float value = 0.0F;
      CopyAndConvertTo(&value, src, src_type);
      WriteValue(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::kDouble: {
      double value = 0.0;
      CopyAndConvertTo(&value, src, src_type);
      WriteValue(dest.subspan(0, kDoubleBytes), value);
      break;
    }
    case PLYPropertyType::kNone:
      break;
    }
  }

  struct TypePair
  {
    PLYPropertyType src;
    PLYPropertyType dest;
  };

  struct RingLinks
  {
    std::span<const uint32_t> prev;
    std::span<const uint32_t> next;
  };

  [[nodiscard]] auto CompatibleTypes(PLYPropertyType src_type, PLYPropertyType dest_type) -> bool
  {
    TypePair const types{ .src = src_type, .dest = dest_type };
    return (types.src == types.dest)
           || (types.src < PLYPropertyType::kFloat
               && (static_cast<uint32_t>(types.src) ^ kSignedPairToggle) == static_cast<uint32_t>(types.dest));
  }

  struct DoubleParseState
  {
    size_t pos = 0;
    double value = 0.0;
    bool has_int_digits = false;
    bool has_frac_digits = false;
  };

  [[nodiscard]] auto IntLiteral(std::span<const char> buffer, size_t start_pos, size_t &end_pos, int *val) -> bool
  {
    if (start_pos >= buffer.size()) { return false; }
    size_t pos = start_pos;

    bool negative = false;
    if (SpanChar(buffer, pos) == '-') {
      negative = true;
      ++pos;
    } else if (pos < buffer.size() && SpanChar(buffer, pos) == '+') {
      ++pos;
    }

    bool const has_leading_zeroes = pos < buffer.size() && SpanChar(buffer, pos) == '0';
    if (has_leading_zeroes) {
      while (pos < buffer.size() && SpanChar(buffer, pos) == '0') { ++pos; }
    }

    int num_digits = 0;
    int local_val = 0;
    while (pos < buffer.size() && IsDigit(SpanChar(buffer, pos))) {
      local_val = (local_val * kDecimalRadix) + static_cast<int>(SpanChar(buffer, pos) - '0');
      ++num_digits;
      ++pos;
    }

    if (num_digits == 0 && has_leading_zeroes) { num_digits = 1; }

    if (num_digits == 0 || pos >= buffer.size() || IsLetter(SpanChar(buffer, pos)) || SpanChar(buffer, pos) == '_') {
      return false;
    }
    if (num_digits > kMaxIntDigits) { return false; }

    if (val != nullptr) { *val = negative ? -local_val : local_val; }
    end_pos = pos;
    return true;
  }

  [[nodiscard]] auto ParseDoubleIntegerPart(std::span<const char> buffer, DoubleParseState &state) -> bool
  {
    state.has_int_digits = state.pos < buffer.size() && IsDigit(SpanChar(buffer, state.pos));
    if (state.has_int_digits) {
      while (state.pos < buffer.size() && IsDigit(SpanChar(buffer, state.pos))) {
        state.value = (state.value * kDecimalBase) + DoubleDigit(SpanChar(buffer, state.pos));
        ++state.pos;
      }
      return true;
    }
    if (state.pos >= buffer.size() || SpanChar(buffer, state.pos) != '.') { return false; }
    return true;
  }

  [[nodiscard]] auto ParseDoubleFractionPart(std::span<const char> buffer, DoubleParseState &state) -> bool
  {
    if (state.pos >= buffer.size() || SpanChar(buffer, state.pos) != '.') { return true; }

    ++state.pos;
    state.has_frac_digits = state.pos < buffer.size() && IsDigit(SpanChar(buffer, state.pos));
    if (!state.has_frac_digits) { return state.has_int_digits; }

    double scale = kFractionScale;
    while (state.pos < buffer.size() && IsDigit(SpanChar(buffer, state.pos))) {
      state.value += scale * DoubleDigit(SpanChar(buffer, state.pos));
      scale *= kFractionScale;
      ++state.pos;
    }
    return true;
  }

  [[nodiscard]] auto ParseDoubleExponent(std::span<const char> buffer, DoubleParseState &state, bool apply_exponent)
    -> bool
  {
    if (state.pos >= buffer.size() || (SpanChar(buffer, state.pos) != 'e' && SpanChar(buffer, state.pos) != 'E')) {
      return true;
    }

    ++state.pos;
    bool negative_exponent = false;
    if (state.pos < buffer.size() && SpanChar(buffer, state.pos) == '-') {
      negative_exponent = true;
      ++state.pos;
    } else if (state.pos < buffer.size() && SpanChar(buffer, state.pos) == '+') {
      ++state.pos;
    }

    if (state.pos >= buffer.size() || !IsDigit(SpanChar(buffer, state.pos))) { return false; }

    double exponent = 0.0;
    while (state.pos < buffer.size() && IsDigit(SpanChar(buffer, state.pos))) {
      exponent = (exponent * kDecimalBase) + DoubleDigit(SpanChar(buffer, state.pos));
      ++state.pos;
    }

    if (apply_exponent) {
      if (negative_exponent) { exponent = -exponent; }
      state.value *= std::pow(kDecimalBase, exponent);
    }
    return true;
  }

  [[nodiscard]] auto DoubleLiteral(std::span<const char> buffer, size_t start_pos, size_t &end_pos, double *val) -> bool
  {
    if (start_pos >= buffer.size()) { return false; }

    DoubleParseState state{};
    state.pos = start_pos;

    bool negative = false;
    if (SpanChar(buffer, state.pos) == '-') {
      negative = true;
      ++state.pos;
    } else if (state.pos < buffer.size() && SpanChar(buffer, state.pos) == '+') {
      ++state.pos;
    }

    if (!ParseDoubleIntegerPart(buffer, state)) { return false; }
    if (!ParseDoubleFractionPart(buffer, state)) { return false; }
    if (!ParseDoubleExponent(buffer, state, val != nullptr)) { return false; }

    if (state.pos < buffer.size()
        && (SpanChar(buffer, state.pos) == '.' || SpanChar(buffer, state.pos) == '_'
            || IsAlnum(SpanChar(buffer, state.pos)))) {
      return false;
    }

    if (negative) { state.value = -state.value; }

    if (val != nullptr) { *val = state.value; }
    end_pos = state.pos;
    return true;
  }

  [[nodiscard]] auto FloatLiteral(std::span<const char> buffer, size_t start_pos, size_t &end_pos, float *val) -> bool
  {
    double parsed = 0.0;
    bool const success = DoubleLiteral(buffer, start_pos, end_pos, &parsed);
    if (success && val != nullptr) { *val = static_cast<float>(parsed); }
    return success;
  }

  [[nodiscard]] auto VertexAt(std::span<const float> pos, int index) -> Vec3
  {
    size_t const base = static_cast<size_t>(index) * kVerticesPerTriangle;
    return Vec3{ .x = SpanAt(pos, base), .y = SpanAt(pos, base + 1U), .z = SpanAt(pos, base + 2U) };
  }

  struct PropertyLayoutInfo
  {
    bool contiguous_cols = true;
    bool contiguous_rows = false;
    bool conversion_required = false;
    uint32_t expected_offset = 0;
  };

  [[nodiscard]] auto ValidatePropertyIndices(PLYElement const &elem, std::span<const uint32_t> prop_idxs) -> bool
  {
    return std::ranges::all_of(
      prop_idxs, [&](uint32_t const prop_idx) -> bool { return prop_idx < elem.properties.size(); });
  }

  [[nodiscard]] auto AnalyzePropertyLayout(PLYElement const &elem,
    std::span<const uint32_t> prop_idxs,
    PLYPropertyType dest_type) -> PropertyLayoutInfo
  {
    PropertyLayoutInfo info{};
    info.expected_offset = elem.properties.at(prop_idxs.front()).offset;
    for (uint32_t const prop_idx : prop_idxs) {
      PLYProperty const &prop = elem.properties.at(prop_idx);
      if (prop.offset != info.expected_offset) {
        info.contiguous_cols = false;
        break;
      }
      info.expected_offset = prop.offset + kPLYPropertySize.at(static_cast<size_t>(prop.type));
    }

    info.contiguous_rows = info.contiguous_cols && (elem.properties.at(prop_idxs.front()).offset == 0U)
                           && (info.expected_offset == elem.row_stride);

    for (uint32_t const prop_idx : prop_idxs) {
      PLYProperty const &prop = elem.properties.at(prop_idx);
      if (!CompatibleTypes(prop.type, dest_type)) {
        info.conversion_required = true;
        break;
      }
    }
    return info;
  }

  void ExtractContiguousRows(std::span<const uint8_t> element_data, size_t num_bytes, std::span<std::byte> dest)
  { std::memcpy(dest.data(), element_data.data(), num_bytes); }

  void ExtractContiguousColumns(std::span<const uint8_t> element_data,
    PLYElement const &elem,
    std::span<const uint32_t> prop_idxs,
    uint32_t expected_offset,
    std::span<std::byte> dest)
  {
    size_t const num_bytes = static_cast<size_t>(expected_offset) - elem.properties.at(prop_idxs.front()).offset;
    size_t dest_offset = 0;
    size_t from_offset = elem.properties.at(prop_idxs.front()).offset;
    while (from_offset < element_data.size()) {
      std::memcpy(
        dest.subspan(dest_offset, num_bytes).data(), element_data.subspan(from_offset, num_bytes).data(), num_bytes);
      from_offset += elem.row_stride;
      dest_offset += num_bytes;
    }
  }

  void ExtractScatteredColumns(std::span<const uint8_t> element_data,
    PLYElement const &elem,
    std::span<const uint32_t> prop_idxs,
    PLYPropertyType dest_type,
    std::span<std::byte> dest)
  {
    size_t const col_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
    size_t dest_offset = 0;
    size_t row_offset = 0;
    while (row_offset < element_data.size()) {
      for (uint32_t const prop_idx : prop_idxs) {
        PLYProperty const &prop = elem.properties.at(prop_idx);
        std::memcpy(dest.subspan(dest_offset, col_bytes).data(),
          element_data.subspan(row_offset + prop.offset, col_bytes).data(),
          col_bytes);
        dest_offset += col_bytes;
      }
      row_offset += elem.row_stride;
    }
  }

  void ConvertScatteredColumns(std::span<const uint8_t> element_data,
    PLYElement const &elem,
    std::span<const uint32_t> prop_idxs,
    PLYPropertyType dest_type,
    std::span<std::byte> dest)
  {
    size_t const col_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
    size_t dest_offset = 0;
    size_t row_offset = 0;
    while (row_offset < element_data.size()) {
      for (uint32_t const prop_idx : prop_idxs) {
        PLYProperty const &prop = elem.properties.at(prop_idx);
        CopyAndConvert(dest.subspan(dest_offset, col_bytes),
          dest_type,
          std::as_bytes(element_data.subspan(row_offset + prop.offset, col_bytes)),
          prop.type);
        dest_offset += col_bytes;
      }
      row_offset += elem.row_stride;
    }
  }

  [[nodiscard]] auto AngleAtVert(uint32_t idx, std::span<const Vec2> points_2d, RingLinks ring) -> float
  {
    Vec2 const xaxis = Normalize(SpanAt(points_2d, SpanAt(ring.next, idx)) - SpanAt(points_2d, idx));
    Vec2 const yaxis = Vec2{ .x = -xaxis.y, .y = xaxis.x };
    Vec2 const p2p0 = SpanAt(points_2d, SpanAt(ring.prev, idx)) - SpanAt(points_2d, idx);
    float angle = std::atan2(Dot(p2p0, yaxis), Dot(p2p0, xaxis));
    if (angle <= 0.0F || angle >= kPi) { angle = kReflexAngle; }
    return angle;
  }

}// namespace

//
// PLYElement methods
//

void PLYElement::calculate_offsets()
{
  fixed_size = !std::ranges::any_of(
    properties, [](PLYProperty const &prop) -> bool { return prop.count_type != PLYPropertyType::kNone; });

  row_stride = 0;
  for (PLYProperty &prop : properties) {
    if (prop.count_type != PLYPropertyType::kNone) { continue; }
    prop.offset = row_stride;
    row_stride += kPLYPropertySize.at(static_cast<size_t>(prop.type));
  }
}


auto PLYElement::find_property(const char *prop_name) const -> uint32_t
{
  for (uint32_t index = 0; index < static_cast<uint32_t>(properties.size()); ++index) {
    if (strcmp(prop_name, properties.at(index).name.c_str()) == 0) { return index; }
  }
  return kInvalidIndex;
}


auto PLYElement::find_properties(std::span<uint32_t> prop_idxs, std::span<const char *const> prop_names) const -> bool
{
  if (prop_idxs.size() != prop_names.size()) { return false; }
  for (size_t index = 0; index < prop_names.size(); ++index) {
    SpanRef(prop_idxs, index) = find_property(SpanAt(prop_names, index));
    if (SpanRef(prop_idxs, index) == kInvalidIndex) { return false; }
  }
  return true;
}


auto PLYElement::convert_list_to_fixed_size(ListPropertyIndex list_prop_idx,
  FixedListSize list_size,
  std::span<uint32_t> new_prop_idxs) -> bool
{
  auto const list_prop_index = static_cast<uint32_t>(list_prop_idx);
  auto const fixed_list_size = static_cast<uint32_t>(list_size);
  if (fixed_size || list_prop_index >= properties.size()
      || properties.at(list_prop_index).count_type == PLYPropertyType::kNone) {
    return false;
  }
  if (new_prop_idxs.size() < fixed_list_size) { return false; }

  PLYProperty const old_list_prop = properties.at(list_prop_index);

  PLYProperty &count_prop = properties.at(list_prop_index);
  count_prop.name = std::format("{}_count", old_list_prop.name);
  count_prop.type = old_list_prop.count_type;
  count_prop.count_type = PLYPropertyType::kNone;
  count_prop.stride = kPLYPropertySize.at(static_cast<size_t>(old_list_prop.count_type));

  if (fixed_list_size > 0U) {
    if (list_prop_index + 1U == static_cast<uint32_t>(properties.size())) {
      properties.resize(properties.size() + fixed_list_size);
    } else {
      size_t const insert_index = static_cast<size_t>(list_prop_index) + 1U;
      properties.insert(properties.begin() + static_cast<std::vector<PLYProperty>::difference_type>(insert_index),
        fixed_list_size,
        PLYProperty{});
    }

    for (uint32_t item_index = 0; item_index < fixed_list_size; ++item_index) {
      uint32_t const prop_idx = list_prop_index + 1U + item_index;
      PLYProperty &item_prop = properties.at(prop_idx);
      item_prop.name = std::format("{}_{}", old_list_prop.name, item_index);
      item_prop.type = old_list_prop.type;
      item_prop.count_type = PLYPropertyType::kNone;
      item_prop.stride = kPLYPropertySize.at(static_cast<size_t>(old_list_prop.type));
      SpanRef(new_prop_idxs, item_index) = prop_idx;
    }
  }

  calculate_offsets();
  return true;
}


//
// PLYReader methods
//

PLYReader::PLYReader(const char *filename)
  : file_(OpenFile(filename, "rb")), buf_(static_cast<size_t>(kPLYReadBufferSize) + 1U, '\0'),
    tmp_buf_(static_cast<size_t>(kPLYTempBufferSize) + 1U, '\0'), pos_(static_cast<size_t>(kPLYReadBufferSize)),
    end_(static_cast<size_t>(kPLYReadBufferSize)), buf_data_end_(static_cast<size_t>(kPLYReadBufferSize))
{
  if (file_ == nullptr) {
    valid_ = false;
    return;
  }
  valid_ = true;

  refill_buffer();

  valid_ = keyword("ply") && next_line() && keyword("format") && advance() && typed_which(kPLYFileTypes, &file_type_)
            && advance() && int_literal(&major_version_) && advance() && match(".") && advance()
            && int_literal(&minor_version_) && next_line() && parse_elements() && keyword("end_header") && advance()
            && match("\n") && accept();
  if (!valid_) { return; }
  in_data_section_ = true;
  if (file_type_ == PLYFileType::kAscii) { advance(); }

  for (PLYElement &elem : elements_) { elem.calculate_offsets(); }
}


PLYReader::~PLYReader() = default;


auto PLYReader::valid() const -> bool { return valid_; }


auto PLYReader::has_element() const -> bool { return valid_ && current_element_ < elements_.size(); }


auto PLYReader::element() const -> const PLYElement *
{
  assert(has_element());
  return &elements_.at(current_element_);
}


auto PLYReader::load_element() -> bool
{
  assert(has_element());
  if (element_loaded_) { return true; }

  PLYElement &elem = elements_.at(current_element_);
  return elem.fixed_size ? load_fixed_size_element(elem) : load_variable_size_element(elem);
}


auto PLYReader::char_at(size_t index) const -> char { return buf_.at(index); }


auto PLYReader::ensure_bytes_available(size_t num_bytes) -> bool
{
  if (pos_ + num_bytes > buf_data_end_) {
    if (!refill_buffer() || pos_ + num_bytes > buf_data_end_) {
      valid_ = false;
      return false;
    }
  }
  return true;
}


void PLYReader::clear_list_property_storage(PLYElement &elem)
{
  for (PLYProperty &prop : elem.properties) {
    if (prop.count_type == PLYPropertyType::kNone) { continue; }
    prop.list_data.clear();
    prop.list_data.shrink_to_fit();
    prop.row_count.clear();
    prop.row_count.shrink_to_fit();
  }
  element_data_.clear();
  element_loaded_ = false;
}


void PLYReader::skip_unloaded_ascii_element(PLYElement const &elem)
{
  for (uint32_t row = 0; row < elem.count; ++row) { next_line(); }
}


void PLYReader::skip_unloaded_binary_fixed_element(PLYElement const &elem)
{
  auto const element_start = static_cast<int64_t>(pos_);
  int64_t const element_size = static_cast<int64_t>(elem.row_stride) * static_cast<int64_t>(elem.count);
  int64_t const element_end = element_start + element_size;
  if (std::cmp_greater_equal(element_end, static_cast<int64_t>(kPLYReadBufferSize))) {
    buf_offset_ += element_end;
    if (!FileSeek(file_.get(), buf_offset_, SEEK_SET)) {
      valid_ = false;
      return;
    }
    buf_data_end_ = static_cast<size_t>(kPLYReadBufferSize);
    pos_ = buf_data_end_;
    end_ = buf_data_end_;
    refill_buffer();
  } else {
    pos_ = static_cast<size_t>(element_end);
    end_ = pos_;
  }
}


void PLYReader::skip_unloaded_binary_variable_element(PLYElement const &elem)
{
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty const &prop : elem.properties) {
      if (prop.count_type == PLYPropertyType::kNone) {
        uint32_t const num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
        if (!ensure_bytes_available(num_bytes)) { return; }
        pos_ += num_bytes;
        end_ = pos_;
        continue;
      }

      uint32_t num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.count_type));
      if (!ensure_bytes_available(num_bytes)) { return; }

      int count = 0;
      CopyAndConvertTo(&count, std::as_bytes(std::span{ buf_ }.subspan(pos_, num_bytes)), prop.count_type);
      if (count < 0) {
        valid_ = false;
        return;
      }

      num_bytes += static_cast<uint32_t>(count) * kPLYPropertySize.at(static_cast<size_t>(prop.type));
      if (!ensure_bytes_available(num_bytes)) { return; }
      pos_ += num_bytes;
      end_ = pos_;
    }
  }
}


void PLYReader::skip_unloaded_binary_big_endian_variable_element(PLYElement const &elem)
{
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty const &prop : elem.properties) {
      if (prop.count_type == PLYPropertyType::kNone) {
        uint32_t const num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
        if (!ensure_bytes_available(num_bytes)) { return; }
        pos_ += num_bytes;
        end_ = pos_;
        continue;
      }

      uint32_t num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.count_type));
      if (!ensure_bytes_available(num_bytes)) { return; }

      int count = 0;
      std::array<std::byte, kScalarValueBytes> tmp{};
      std::memcpy(tmp.data(), std::span{ buf_ }.subspan(pos_, num_bytes).data(), num_bytes);
      EndianSwap(std::span{ tmp }.subspan(0, num_bytes), prop.count_type);
      CopyAndConvertTo(&count, std::span<const std::byte>{ tmp }.subspan(0, num_bytes), prop.count_type);
      if (count < 0) {
        valid_ = false;
        return;
      }

      num_bytes += static_cast<uint32_t>(count) * kPLYPropertySize.at(static_cast<size_t>(prop.type));
      if (!ensure_bytes_available(num_bytes)) { return; }
      pos_ += num_bytes;
      end_ = pos_;
    }
  }
}


void PLYReader::next_element()
{
  if (!has_element()) { return; }

  PLYElement &elem = elements_.at(current_element_);
  current_element_++;

  if (element_loaded_) {
    clear_list_property_storage(elem);
    return;
  }

  if (file_type_ == PLYFileType::kAscii) {
    skip_unloaded_ascii_element(elem);
  } else if (elem.fixed_size) {
    skip_unloaded_binary_fixed_element(elem);
  } else if (file_type_ == PLYFileType::kBinary) {
    skip_unloaded_binary_variable_element(elem);
  } else {
    skip_unloaded_binary_big_endian_variable_element(elem);
  }
}


auto PLYReader::file_type() const -> PLYFileType { return file_type_; }


auto PLYReader::version_major() const -> int { return major_version_; }


auto PLYReader::version_minor() const -> int { return minor_version_; }


auto PLYReader::num_elements() const -> uint32_t { return valid_ ? static_cast<uint32_t>(elements_.size()) : 0U; }


auto PLYReader::find_element(const char *name) const -> uint32_t
{
  for (uint32_t index = 0; index < num_elements(); ++index) {
    if (strcmp(elements_.at(index).name.c_str(), name) == 0) { return index; }
  }
  return kInvalidIndex;
}


auto PLYReader::get_element(uint32_t idx) -> PLYElement *
{ return (idx < num_elements()) ? &elements_.at(idx) : nullptr; }


auto PLYReader::element_is(const char *name) const -> bool
{ return has_element() && strcmp(element()->name.c_str(), name) == 0; }


auto PLYReader::num_rows() const -> uint32_t { return has_element() ? element()->count : 0U; }


auto PLYReader::find_property(const char *name) const -> uint32_t
{ return has_element() ? element()->find_property(name) : kInvalidIndex; }


auto PLYReader::find_properties(std::span<uint32_t> prop_idxs, std::span<const char *const> prop_names) const -> bool
{
  if (!has_element()) { return false; }
  return element()->find_properties(prop_idxs, prop_names);
}


auto PLYReader::extract_properties(std::span<const uint32_t> prop_idxs, PLYPropertyType dest_type, void *dest) const
  -> bool
{
  if (prop_idxs.empty() || dest == nullptr) { return false; }

  PLYElement const *elem = element();
  if (!ValidatePropertyIndices(*elem, prop_idxs)) { return false; }

  PropertyLayoutInfo const layout = AnalyzePropertyLayout(*elem, prop_idxs, dest_type);
  auto dest_bytes = std::span{ static_cast<std::byte *>(dest), element_data_.size() };

  if (!layout.conversion_required) {
    if (layout.contiguous_rows) {
      ExtractContiguousRows(element_data_, element_data_.size(), dest_bytes);
    } else if (layout.contiguous_cols) {
      ExtractContiguousColumns(element_data_, *elem, prop_idxs, layout.expected_offset, dest_bytes);
    } else {
      ExtractScatteredColumns(element_data_, *elem, prop_idxs, dest_type, dest_bytes);
    }
  } else {
    ConvertScatteredColumns(element_data_, *elem, prop_idxs, dest_type, dest_bytes);
  }

  return true;
}


auto PLYReader::extract_properties_with_stride(std::span<const uint32_t> prop_idxs,
  PLYPropertyType dest_type,
  void *dest,
  uint32_t dest_stride) const -> bool
{
  if (prop_idxs.empty() || dest == nullptr) { return false; }

  uint32_t const min_dest_stride =
    static_cast<uint32_t>(prop_idxs.size()) * kPLYPropertySize.at(static_cast<size_t>(dest_type));
  if (dest_stride == 0U || dest_stride == min_dest_stride) { return extract_properties(prop_idxs, dest_type, dest); }
  if (dest_stride < min_dest_stride) { return false; }

  PLYElement const *elem = element();
  if (!ValidatePropertyIndices(*elem, prop_idxs)) { return false; }

  PropertyLayoutInfo const layout = AnalyzePropertyLayout(*elem, prop_idxs, dest_type);
  size_t const col_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
  size_t const col_padding = static_cast<size_t>(dest_stride) - static_cast<size_t>(min_dest_stride);
  size_t dest_offset = 0;
  size_t row_offset = 0;

  while (row_offset < element_data_.size()) {
    if (!layout.conversion_required && layout.contiguous_cols) {
      size_t const num_bytes =
        static_cast<size_t>(layout.expected_offset) - elem->properties.at(prop_idxs.front()).offset;
      std::memcpy(ByteSpan(dest, element_data_.size()).subspan(dest_offset, num_bytes).data(),
        std::span{ element_data_ }
          .subspan(row_offset + elem->properties.at(prop_idxs.front()).offset, num_bytes)
          .data(),
        num_bytes);
    } else {
      for (uint32_t const prop_idx : prop_idxs) {
        PLYProperty const &prop = elem->properties.at(prop_idx);
        auto dest_span = ByteSpan(dest, element_data_.size()).subspan(dest_offset, col_bytes);
        auto src_span = std::as_bytes(std::span{ element_data_ }.subspan(row_offset + prop.offset, col_bytes));
        if (layout.conversion_required) {
          CopyAndConvert(dest_span, dest_type, src_span, prop.type);
        } else {
          std::memcpy(dest_span.data(), src_span.data(), col_bytes);
        }
        dest_offset += col_bytes;
      }
      dest_offset += col_padding;
      row_offset += elem->row_stride;
      continue;
    }
    dest_offset += static_cast<size_t>(dest_stride);
    row_offset += elem->row_stride;
  }

  return true;
}


auto PLYReader::get_list_counts(uint32_t prop_idx) const -> const uint32_t *
{
  if (!has_element() || prop_idx >= element()->properties.size()
      || element()->properties.at(prop_idx).count_type == PLYPropertyType::kNone) {
    return nullptr;
  }
  return element()->properties.at(prop_idx).row_count.data();
}


auto PLYReader::sum_of_list_counts(uint32_t prop_idx) const -> uint32_t
{
  if (!has_element() || prop_idx >= element()->properties.size()
      || element()->properties.at(prop_idx).count_type == PLYPropertyType::kNone) {
    return 0U;
  }
  PLYProperty const &prop = element()->properties.at(prop_idx);
  return static_cast<uint32_t>(prop.list_data.size() / kPLYPropertySize.at(static_cast<size_t>(prop.type)));
}


auto PLYReader::get_list_data(uint32_t prop_idx) const -> const uint8_t *
{
  if (!has_element() || prop_idx >= element()->properties.size()
      || element()->properties.at(prop_idx).count_type == PLYPropertyType::kNone) {
    return nullptr;
  }
  return element()->properties.at(prop_idx).list_data.data();
}


auto PLYReader::extract_list_property(uint32_t prop_idx, PLYPropertyType dest_type, void *dest) const -> bool
{
  if (!has_element() || prop_idx >= element()->properties.size()
      || element()->properties.at(prop_idx).count_type == PLYPropertyType::kNone || dest == nullptr) {
    return false;
  }

  PLYProperty const &prop = element()->properties.at(prop_idx);
  if (CompatibleTypes(prop.type, dest_type)) {
    std::memcpy(dest, prop.list_data.data(), prop.list_data.size());
    return true;
  }

  size_t dest_offset = 0;
  size_t from_offset = 0;
  size_t const to_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
  size_t const from_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  while (from_offset < prop.list_data.size()) {
    CopyAndConvert(ByteSpan(dest, prop.list_data.size()).subspan(dest_offset, to_bytes),
      dest_type,
      std::as_bytes(std::span{ prop.list_data }.subspan(from_offset, from_bytes)),
      prop.type);
    dest_offset += to_bytes;
    from_offset += from_bytes;
  }

  return true;
}


auto PLYReader::num_triangles(uint32_t prop_idx) const -> uint32_t
{
  uint32_t const *counts = get_list_counts(prop_idx);
  if (counts == nullptr) { return 0U; }

  std::span<const uint32_t> const count_span{ counts, element()->count };
  uint32_t num = 0U;
  for (uint32_t row = 0; row < element()->count; ++row) {
    if (SpanAt(count_span, row) >= kVerticesPerTriangle) { num += SpanAt(count_span, row) - 2U; }
  }
  return num;
}


auto PLYReader::requires_triangulation(uint32_t prop_idx) const -> bool
{
  uint32_t const *counts = get_list_counts(prop_idx);
  if (counts == nullptr) { return false; }

  std::span<const uint32_t> const count_span{ counts, element()->count };
  for (uint32_t row = 0; row < element()->count; ++row) {
    if (SpanAt(count_span, row) != kVerticesPerTriangle) { return true; }
  }
  return false;
}


auto PLYReader::extract_triangles(uint32_t prop_idx,
  std::span<const float> positions,
  MeshVertexCount mesh_vertex_count,
  PLYPropertyType dest_type,
  void *dest) const -> bool
{
  if (dest == nullptr) { return false; }
  if (!requires_triangulation(prop_idx)) { return extract_list_property(prop_idx, dest_type, dest); }

  PLYProperty const &prop = element()->properties.at(prop_idx);
  bool const convert_src = !CompatibleTypes(prop.type, PLYPropertyType::kInt);
  bool const convert_dst = !CompatibleTypes(PLYPropertyType::kInt, dest_type);

  if (convert_src && convert_dst) {
    return extract_triangles_convert_both(prop_idx, positions, mesh_vertex_count, dest_type, dest);
  }
  if (convert_src) { return extract_triangles_convert_src(prop_idx, positions, mesh_vertex_count, dest_type, dest); }
  if (convert_dst) { return extract_triangles_convert_dst(prop_idx, positions, mesh_vertex_count, dest_type, dest); }
  return extract_triangles_native(prop_idx, positions, mesh_vertex_count, dest_type, dest);
}


auto PLYReader::extract_triangles_convert_both(uint32_t prop_idx,
  std::span<const float> positions,
  MeshVertexCount mesh_vertex_count,
  PLYPropertyType dest_type,
  void *dest) const -> bool
{
  PLYElement const *elem = element();
  PLYProperty const &prop = elem->properties.at(prop_idx);
  std::span<const uint32_t> const counts = prop.row_count;
  std::span<const uint8_t> const data = prop.list_data;
  size_t dest_byte_offset = 0;
  size_t const src_val_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const dest_val_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
  size_t face_offset = 0;
  auto dest_bytes = ByteSpan(dest, data.size());
  std::vector<int> tri_indices;
  tri_indices.reserve(kDefaultTriIndexReserve);

  for (uint32_t face_idx = 0; face_idx < elem->count; ++face_idx) {
    std::vector<int> face_indices;
    face_indices.reserve(SpanAt(counts, face_idx));
    for (uint32_t vertex_index = 0; vertex_index < SpanAt(counts, face_idx); ++vertex_index) {
      int idx = 0;
      CopyAndConvertTo(&idx, std::as_bytes(std::span{ data }.subspan(face_offset, src_val_bytes)), prop.type);
      face_indices.push_back(idx);
      face_offset += src_val_bytes;
    }
    tri_indices.resize(static_cast<size_t>(SpanAt(counts, face_idx) - 2U) * kIndicesPerTriangle);
    TriangulatePolygon(PolygonVertexCount{ SpanAt(counts, face_idx) },
      positions,
      mesh_vertex_count,
      PolygonIndices{ face_indices },
      TriangleDestination{ tri_indices });
    for (int const idx : tri_indices) {
      std::array<std::byte, kScalarValueBytes> idx_bytes{};
      WriteValue(std::span{ idx_bytes }.subspan(0, sizeof(int)), idx);
      CopyAndConvert(dest_bytes.subspan(dest_byte_offset, dest_val_bytes),
        dest_type,
        std::span<const std::byte>{ idx_bytes }.subspan(0, sizeof(int)),
        PLYPropertyType::kInt);
      dest_byte_offset += dest_val_bytes;
    }
  }
  return true;
}


auto PLYReader::extract_triangles_convert_src(uint32_t prop_idx,
  std::span<const float> positions,
  MeshVertexCount mesh_vertex_count,
  PLYPropertyType dest_type,
  void *dest) const -> bool
{
  PLYElement const *elem = element();
  PLYProperty const &prop = elem->properties.at(prop_idx);
  std::span<const uint32_t> const counts = prop.row_count;
  std::span<const uint8_t> const data = prop.list_data;
  size_t dest_byte_offset = 0;
  size_t const src_val_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const dest_val_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
  size_t face_offset = 0;
  auto dest_bytes = ByteSpan(dest, data.size());
  std::span<int> const dest_ints(static_cast<int *>(dest), dest_bytes.size() / sizeof(int));

  for (uint32_t face_idx = 0; face_idx < elem->count; ++face_idx) {
    std::vector<int> face_indices;
    face_indices.reserve(SpanAt(counts, face_idx));
    for (uint32_t vertex_index = 0; vertex_index < SpanAt(counts, face_idx); ++vertex_index) {
      int idx = 0;
      CopyAndConvertTo(&idx, std::as_bytes(std::span{ data }.subspan(face_offset, src_val_bytes)), prop.type);
      face_indices.push_back(idx);
      face_offset += src_val_bytes;
    }
    size_t const tri_capacity = static_cast<size_t>(SpanAt(counts, face_idx) - 2U) * kIndicesPerTriangle;
    uint32_t const num_tris = TriangulatePolygon(PolygonVertexCount{ SpanAt(counts, face_idx) },
      positions,
      mesh_vertex_count,
      PolygonIndices{ face_indices },
      TriangleDestination{ dest_ints.subspan(dest_byte_offset / sizeof(int), tri_capacity) });
    dest_byte_offset += static_cast<size_t>(num_tris) * kIndicesPerTriangle * dest_val_bytes;
  }
  return true;
}


auto PLYReader::extract_triangles_convert_dst(uint32_t prop_idx,
  std::span<const float> positions,
  MeshVertexCount mesh_vertex_count,
  PLYPropertyType dest_type,
  void *dest) const -> bool
{
  PLYElement const *elem = element();
  PLYProperty const &prop = elem->properties.at(prop_idx);
  std::span<const uint32_t> const counts = prop.row_count;
  std::span<const uint8_t> const data = prop.list_data;
  size_t dest_byte_offset = 0;
  size_t const src_val_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const dest_val_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
  size_t face_offset = 0;
  auto dest_bytes = ByteSpan(dest, data.size());
  std::vector<int> tri_indices;
  tri_indices.reserve(kDefaultTriIndexReserve);

  for (uint32_t face_idx = 0; face_idx < elem->count; ++face_idx) {
    std::vector<int> face_indices;
    face_indices.reserve(SpanAt(counts, face_idx));
    for (uint32_t vertex_index = 0; vertex_index < SpanAt(counts, face_idx); ++vertex_index) {
      int const idx = ReadValue<int32_t>(std::as_bytes(std::span{ data }.subspan(face_offset, src_val_bytes)));
      face_indices.push_back(idx);
      face_offset += src_val_bytes;
    }
    tri_indices.resize(static_cast<size_t>(SpanAt(counts, face_idx) - 2U) * kIndicesPerTriangle);
    TriangulatePolygon(PolygonVertexCount{ SpanAt(counts, face_idx) },
      positions,
      mesh_vertex_count,
      PolygonIndices{ face_indices },
      TriangleDestination{ tri_indices });
    for (int const idx : tri_indices) {
      std::array<std::byte, kScalarValueBytes> idx_bytes{};
      WriteValue(std::span{ idx_bytes }.subspan(0, sizeof(int)), idx);
      CopyAndConvert(dest_bytes.subspan(dest_byte_offset, dest_val_bytes),
        dest_type,
        std::span<const std::byte>{ idx_bytes }.subspan(0, sizeof(int)),
        PLYPropertyType::kInt);
      dest_byte_offset += dest_val_bytes;
    }
  }
  return true;
}


auto PLYReader::extract_triangles_native(uint32_t prop_idx,
  std::span<const float> positions,
  MeshVertexCount mesh_vertex_count,
  PLYPropertyType dest_type,
  void *dest) const -> bool
{
  PLYElement const *elem = element();
  PLYProperty const &prop = elem->properties.at(prop_idx);
  std::span<const uint32_t> const counts = prop.row_count;
  std::span<const uint8_t> const data = prop.list_data;
  size_t dest_byte_offset = 0;
  size_t const src_val_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const dest_val_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
  size_t face_offset = 0;
  auto dest_bytes = ByteSpan(dest, data.size());
  std::span<int> const dest_ints(static_cast<int *>(dest), dest_bytes.size() / sizeof(int));

  for (uint32_t face_idx = 0; face_idx < elem->count; ++face_idx) {
    std::vector<int> face_indices;
    face_indices.reserve(SpanAt(counts, face_idx));
    for (uint32_t vertex_index = 0; vertex_index < SpanAt(counts, face_idx); ++vertex_index) {
      face_indices.push_back(ReadValue<int32_t>(std::as_bytes(std::span{ data }.subspan(face_offset, src_val_bytes))));
      face_offset += src_val_bytes;
    }
    size_t const tri_capacity = static_cast<size_t>(SpanAt(counts, face_idx) - 2U) * kIndicesPerTriangle;
    uint32_t const num_tris = TriangulatePolygon(PolygonVertexCount{ SpanAt(counts, face_idx) },
      positions,
      mesh_vertex_count,
      PolygonIndices{ face_indices },
      TriangleDestination{ dest_ints.subspan(dest_byte_offset / sizeof(int), tri_capacity) });
    dest_byte_offset += static_cast<size_t>(num_tris) * kIndicesPerTriangle * dest_val_bytes;
  }
  return true;
}


auto PLYReader::find_pos(std::span<uint32_t, 3> prop_idxs) const -> bool
{
  static constexpr std::array<const char *, 3> kNames{ "x", "y", "z" };
  return find_properties(prop_idxs, kNames);
}


auto PLYReader::find_normal(std::span<uint32_t, 3> prop_idxs) const -> bool
{
  static constexpr std::array<const char *, 3> kNames{ "nx", "ny", "nz" };
  return find_properties(prop_idxs, kNames);
}


auto PLYReader::find_texcoord(std::span<uint32_t, 2> prop_idxs) const -> bool
{
  static constexpr std::array<const char *, 2> kUvNames{ "u", "v" };
  static constexpr std::array<const char *, 2> kStNames{ "s", "t" };
  static constexpr std::array<const char *, 2> kTextureUvNames{ "texture_u", "texture_v" };
  static constexpr std::array<const char *, 2> kTextureStNames{ "texture_s", "texture_t" };
  return find_properties(prop_idxs, kUvNames) || find_properties(prop_idxs, kStNames)
         || find_properties(prop_idxs, kTextureUvNames) || find_properties(prop_idxs, kTextureStNames);
}


auto PLYReader::find_color(std::span<uint32_t, 3> prop_idxs) const -> bool
{
  static constexpr std::array<const char *, 3> kShortNames{ "r", "g", "b" };
  static constexpr std::array<const char *, 3> kLongNames{ "red", "green", "blue" };
  return find_properties(prop_idxs, kShortNames) || find_properties(prop_idxs, kLongNames);
}


auto PLYReader::find_indices(std::span<uint32_t, 1> prop_idxs) const -> bool
{
  static constexpr std::array<const char *, 1> kPluralNames{ "vertex_indices" };
  static constexpr std::array<const char *, 1> kSingularNames{ "vertex_index" };
  return find_properties(prop_idxs, kPluralNames) || find_properties(prop_idxs, kSingularNames);
}


auto PLYReader::refill_buffer() -> bool
{
  if (file_ == nullptr || at_eof_) { return false; }

  if (pos_ == 0U && end_ == buf_data_end_ && buf_data_end_ == static_cast<size_t>(kPLYReadBufferSize)) {
    return false;
  }

  auto const buf_size = static_cast<int64_t>(buf_data_end_);
  if (std::cmp_less(buf_size, static_cast<int64_t>(kPLYReadBufferSize))) {

    buf_.at(static_cast<size_t>(kPLYReadBufferSize)) = '\0';
    buf_data_end_ = static_cast<size_t>(kPLYReadBufferSize);
  }

  size_t const keep = buf_data_end_ - pos_;
  if (keep > 0U && pos_ > 0U) {
    std::memmove(buf_.data(), std::span{ buf_ }.subspan(pos_, keep).data(), keep);
    buf_offset_ += static_cast<int64_t>(pos_);
  }
  if (end_ >= pos_) {
    end_ -= pos_;
  } else {
    end_ = 0U;
  }
  pos_ = 0U;

  size_t const read_size = static_cast<size_t>(kPLYReadBufferSize) - keep;
  size_t const read_count =
    fread(std::span{ buf_ }.subspan(keep, read_size).data(), sizeof(char), read_size, file_.get());
  if (read_count < read_size && ferror(file_.get()) != 0) { return false; }

  size_t const fetched = read_count + keep;
  at_eof_ = fetched < static_cast<size_t>(kPLYReadBufferSize);
  buf_data_end_ = fetched;

  if (!in_data_section_ || file_type_ == PLYFileType::kAscii) { return rewind_to_safe_char(); }
  return true;
}


auto PLYReader::rewind_to_safe_char() -> bool
{
  if (!at_eof_ && buf_data_end_ > 0U
      && (buf_.at(buf_data_end_ - 1U) == '\n' || !IsSafeBufferEnd(buf_.at(buf_data_end_ - 1U)))) {
    size_t safe = buf_data_end_ - 2U;
    while (safe >= end_ && (buf_.at(safe) == '\n' || !IsSafeBufferEnd(buf_.at(safe)))) {
      if (safe == 0U) { break; }
      --safe;
    }
    if (safe < end_) { return false; }
    ++safe;
    buf_.at(static_cast<size_t>(kPLYReadBufferSize)) = buf_.at(safe);
    buf_data_end_ = safe;
  }
  buf_.at(buf_data_end_) = '\0';
  return true;
}


auto PLYReader::accept() -> bool
{
  pos_ = end_;
  return true;
}


auto PLYReader::advance() -> bool
{
  pos_ = end_;
  while (true) {
    while (pos_ < buf_data_end_ && IsWhitespace(char_at(pos_))) { ++pos_; }
    if (pos_ == buf_data_end_) {
      end_ = pos_;
      if (refill_buffer()) { continue; }
      return false;
    }
    break;
  }
  end_ = pos_;
  return true;
}


auto PLYReader::next_line() -> bool
{
  pos_ = end_;
  while (true) {
    while (pos_ < buf_data_end_ && char_at(pos_) != '\n') { ++pos_; }
    if (pos_ == buf_data_end_) {
      end_ = pos_;
      if (refill_buffer()) { continue; }
      return false;
    }
    ++pos_;
    end_ = pos_;
    if (!match("comment") && !match("obj_info")) { return true; }
  }
}


auto PLYReader::match(std::string_view str) -> bool
{
  end_ = pos_;
  size_t str_index = 0U;
  while (end_ < buf_data_end_ && str_index < str.size() && char_at(end_) == str.at(str_index)) {
    ++end_;
    ++str_index;
  }
  return str_index == str.size();
}


auto PLYReader::which(std::span<const std::string_view> values, uint32_t *index) -> bool
{
  for (uint32_t value_index = 0; value_index < static_cast<uint32_t>(values.size()); ++value_index) {
    if (keyword(SpanAt(values, value_index))) {
      *index = value_index;
      return true;
    }
  }
  return false;
}


auto PLYReader::which_property_type(PLYPropertyType *type) -> bool
{
  for (PLYTypeAlias const &alias : kTypeAliases) {
    if (alias.name.empty()) { break; }
    if (keyword(alias.name)) {
      *type = alias.type;
      return true;
    }
  }
  return false;
}


auto PLYReader::keyword(std::string_view keyword_text) -> bool
{ return match(keyword_text) && (end_ >= buf_data_end_ || !IsKeywordPart(char_at(end_))); }


auto PLYReader::identifier(std::span<char> dest) -> bool
{
  end_ = pos_;
  if (dest.empty() || end_ >= buf_data_end_ || !IsKeywordStart(char_at(end_))) { return false; }
  while (end_ < buf_data_end_ && IsKeywordPart(char_at(end_))) { ++end_; }

  size_t const len = end_ - pos_;
  if (len >= dest.size()) { return false; }
  std::memcpy(dest.data(), std::span{ buf_ }.subspan(pos_, len).data(), len);
  SpanRef(dest, len) = '\0';
  return true;
}


auto PLYReader::int_literal(int *value) -> bool
{
  size_t end_pos = pos_;
  bool const success = miniply::IntLiteral(std::span{ buf_ }.subspan(0, buf_data_end_), pos_, end_pos, value);
  if (success) { end_ = end_pos; }
  return success;
}


auto PLYReader::float_literal(float *value) -> bool
{
  size_t end_pos = pos_;
  bool const success = miniply::FloatLiteral(std::span{ buf_ }.subspan(0, buf_data_end_), pos_, end_pos, value);
  if (success) { end_ = end_pos; }
  return success;
}


auto PLYReader::double_literal(double *value) -> bool
{
  size_t end_pos = pos_;
  bool const success = miniply::DoubleLiteral(std::span{ buf_ }.subspan(0, buf_data_end_), pos_, end_pos, value);
  if (success) { end_ = end_pos; }
  return success;
}


auto PLYReader::parse_elements() -> bool
{
  elements_.reserve(kElementReserve);
  while (valid_ && keyword("element")) { parse_element(); }
  return true;
}


auto PLYReader::parse_element() -> bool
{
  int count = 0;
  auto tmp_span = std::span{ tmp_buf_ }.subspan(0, static_cast<size_t>(kPLYTempBufferSize));

  valid_ = keyword("element") && advance() && identifier(tmp_span) && advance() && int_literal(&count) && next_line();
  if (!valid_ || count < 0) { return false; }

  elements_.emplace_back();
  PLYElement &elem = elements_.back();
  elem.name = tmp_buf_.data();
  elem.count = static_cast<uint32_t>(count);
  elem.properties.reserve(kPropertyReserve);

  while (valid_ && keyword("property")) { parse_property(elem.properties); }
  return true;
}


auto PLYReader::parse_property(std::vector<PLYProperty> &properties) -> bool
{
  PLYPropertyType type = PLYPropertyType::kNone;
  PLYPropertyType count_type = PLYPropertyType::kNone;

  valid_ = keyword("property") && advance();
  if (!valid_) { return false; }

  if (keyword("list")) {
    valid_ = advance() && which_property_type(&count_type) && advance();
    if (!valid_) { return false; }
  }

  auto tmp_span = std::span{ tmp_buf_ }.subspan(0, static_cast<size_t>(kPLYTempBufferSize));
  valid_ = which_property_type(&type) && advance() && identifier(tmp_span) && next_line();
  if (!valid_) { return false; }

  properties.emplace_back();
  PLYProperty &prop = properties.back();
  prop.name = tmp_buf_.data();
  prop.type = type;
  prop.count_type = count_type;
  return true;
}


auto PLYReader::load_fixed_size_element(PLYElement &elem) -> bool
{
  size_t const num_bytes = static_cast<size_t>(elem.count) * static_cast<size_t>(elem.row_stride);
  element_data_.resize(num_bytes);

  if (file_type_ == PLYFileType::kAscii) {
    if (!load_fixed_ascii_element(elem)) { return false; }
  } else if (!load_fixed_binary_element(elem, num_bytes)) {
    return false;
  }

  element_loaded_ = true;
  return true;
}


auto PLYReader::load_fixed_ascii_element(PLYElement &elem) -> bool
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (!load_ascii_scalar_property(prop, back)) {
        valid_ = false;
        return false;
      }
    }
    next_line();
  }
  return true;
}


auto PLYReader::load_fixed_binary_element(PLYElement const &elem, size_t num_bytes) -> bool
{
  size_t dst_offset = 0U;
  while (dst_offset < num_bytes) {
    size_t bytes_available = buf_data_end_ - pos_;
    if (dst_offset + bytes_available > num_bytes) { bytes_available = num_bytes - dst_offset; }
    std::memcpy(std::span{ element_data_ }.subspan(dst_offset, bytes_available).data(),
      std::span{ buf_ }.subspan(pos_, bytes_available).data(),
      bytes_available);
    pos_ += bytes_available;
    end_ = pos_;
    dst_offset += bytes_available;
    if (!refill_buffer()) { break; }
  }
  if (dst_offset < num_bytes) {
    valid_ = false;
    return false;
  }

  if (file_type_ == PLYFileType::kBinaryBigEndian) { endian_swap_loaded_fixed_element(elem); }
  return true;
}


void PLYReader::endian_swap_loaded_fixed_element(PLYElement const &elem)
{
  size_t data_offset = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty const &prop : elem.properties) {
      size_t const prop_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
      EndianSwap(std::as_writable_bytes(std::span{ element_data_ }.subspan(data_offset, prop_bytes)), prop.type);
      data_offset += prop_bytes;
    }
  }
}


auto PLYReader::load_variable_size_element(PLYElement &elem) -> bool
{
  element_data_.resize(static_cast<size_t>(elem.count) * static_cast<size_t>(elem.row_stride));

  for (PLYProperty &prop : elem.properties) {
    if (prop.count_type != PLYPropertyType::kNone) {
      prop.list_data.reserve(static_cast<size_t>(elem.count)
                             * static_cast<size_t>(kPLYPropertySize.at(static_cast<size_t>(prop.type))) * 3U);
    }
  }

  if (file_type_ == PLYFileType::kBinary) {
    if (!load_variable_binary_element(elem)) { return false; }
  } else if (file_type_ == PLYFileType::kAscii) {
    if (!load_variable_ascii_element(elem)) { return false; }
  } else if (!load_variable_binary_big_endian_element(elem)) {
    return false;
  }

  element_loaded_ = true;
  return true;
}


auto PLYReader::load_variable_binary_element(PLYElement &elem) -> bool
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (prop.count_type == PLYPropertyType::kNone) {
        valid_ = load_binary_scalar_property(prop, back);
      } else {
        load_binary_list_property(prop);
      }
    }
  }
  return valid_;
}


auto PLYReader::load_variable_ascii_element(PLYElement &elem) -> bool
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (prop.count_type == PLYPropertyType::kNone) {
        valid_ = load_ascii_scalar_property(prop, back);
      } else {
        load_ascii_list_property(prop);
      }
    }
    next_line();
  }
  return valid_;
}


auto PLYReader::load_variable_binary_big_endian_element(PLYElement &elem) -> bool
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (prop.count_type == PLYPropertyType::kNone) {
        valid_ = load_binary_scalar_property_big_endian(prop, back);
      } else {
        load_binary_list_property_big_endian(prop);
      }
    }
  }
  return valid_;
}


auto PLYReader::load_ascii_scalar_property(PLYProperty &prop, size_t &dest_index) -> bool
{
  std::array<uint8_t, kScalarValueBytes> value{};
  if (!ascii_value(prop.type, value)) { return false; }

  size_t const num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  std::memcpy(std::span{ element_data_ }.subspan(dest_index, num_bytes).data(), value.data(), num_bytes);
  dest_index += num_bytes;
  return true;
}


auto PLYReader::load_ascii_list_property(PLYProperty &prop) -> bool
{
  int count = 0;
  valid_ = (prop.count_type < PLYPropertyType::kFloat) && int_literal(&count) && advance() && (count >= 0);
  if (!valid_) { return false; }

  size_t const num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const back = prop.list_data.size();
  prop.row_count.push_back(static_cast<uint32_t>(count));
  prop.list_data.resize(back + (num_bytes * static_cast<size_t>(count)));

  for (int item_index = 0; item_index < count; ++item_index) {
    if (!ascii_value(prop.type,
          std::span{ prop.list_data }.subspan(
            back + (static_cast<size_t>(item_index) * num_bytes), kScalarValueBytes))) {
      valid_ = false;
      return false;
    }
  }
  return true;
}


auto PLYReader::load_binary_scalar_property(PLYProperty &prop, size_t &dest_index) -> bool
{
  size_t const num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  if (!ensure_bytes_available(num_bytes)) { return false; }
  std::memcpy(std::span{ element_data_ }.subspan(dest_index, num_bytes).data(),
    std::span{ buf_ }.subspan(pos_, num_bytes).data(),
    num_bytes);
  pos_ += num_bytes;
  end_ = pos_;
  dest_index += num_bytes;
  return true;
}


auto PLYReader::load_binary_list_property(PLYProperty &prop) -> bool
{
  size_t const count_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.count_type));
  if (!ensure_bytes_available(count_bytes)) { return false; }

  int count = 0;
  CopyAndConvertTo(&count, std::as_bytes(std::span{ buf_ }.subspan(pos_, count_bytes)), prop.count_type);
  if (count < 0) {
    valid_ = false;
    return false;
  }

  pos_ += count_bytes;
  end_ = pos_;

  size_t const list_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type)) * static_cast<size_t>(count);
  if (!ensure_bytes_available(list_bytes)) { return false; }

  size_t const back = prop.list_data.size();
  prop.row_count.push_back(static_cast<uint32_t>(count));
  prop.list_data.resize(back + list_bytes);
  std::memcpy(std::span{ prop.list_data }.subspan(back, list_bytes).data(),
    std::span{ buf_ }.subspan(pos_, list_bytes).data(),
    list_bytes);

  pos_ += list_bytes;
  end_ = pos_;
  return true;
}


auto PLYReader::load_binary_scalar_property_big_endian(PLYProperty &prop, size_t &dest_index) -> bool
{
  size_t const start_index = dest_index;
  if (load_binary_scalar_property(prop, dest_index)) {
    EndianSwap(std::as_writable_bytes(
                 std::span{ element_data_ }.subspan(start_index, kPLYPropertySize.at(static_cast<size_t>(prop.type)))),
      prop.type);
    return true;
  }
  return false;
}


auto PLYReader::load_binary_list_property_big_endian(PLYProperty &prop) -> bool
{
  size_t const count_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.count_type));
  if (!ensure_bytes_available(count_bytes)) { return false; }

  int count = 0;
  std::array<std::byte, kScalarValueBytes> tmp{};
  std::memcpy(tmp.data(), std::span{ buf_ }.subspan(pos_, count_bytes).data(), count_bytes);
  EndianSwap(std::span{ tmp }.subspan(0, count_bytes), prop.count_type);
  CopyAndConvertTo(&count, std::span<const std::byte>{ tmp }.subspan(0, count_bytes), prop.count_type);
  if (count < 0) {
    valid_ = false;
    return false;
  }

  pos_ += count_bytes;
  end_ = pos_;

  size_t const type_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const list_bytes = type_bytes * static_cast<size_t>(count);
  if (!ensure_bytes_available(list_bytes)) { return false; }

  size_t const back = prop.list_data.size();
  prop.row_count.push_back(static_cast<uint32_t>(count));
  prop.list_data.resize(back + list_bytes);

  std::memcpy(std::span{ prop.list_data }.subspan(back, list_bytes).data(),
    std::span{ buf_ }.subspan(pos_, list_bytes).data(),
    list_bytes);
  EndianSwapArray(std::as_writable_bytes(std::span{ prop.list_data }.subspan(back, list_bytes)), prop.type, count);

  pos_ += list_bytes;
  end_ = pos_;
  return true;
}


auto PLYReader::ascii_value(PLYPropertyType prop_type, std::span<uint8_t> value) -> bool
{
  auto value_bytes = std::as_writable_bytes(value.subspan(0, kScalarValueBytes));
  int tmp_int = 0;

  switch (prop_type) {
  case PLYPropertyType::kChar:
  case PLYPropertyType::kUChar:
  case PLYPropertyType::kShort:
  case PLYPropertyType::kUShort:
    valid_ = int_literal(&tmp_int);
    break;
  case PLYPropertyType::kInt:
  case PLYPropertyType::kUInt: {
    int parsed = 0;
    valid_ = int_literal(&parsed);
    if (valid_) { WriteValue(value_bytes.subspan(0, kInt32Bytes), static_cast<uint32_t>(parsed)); }
    break;
  }
  case PLYPropertyType::kFloat: {
    float parsed = 0.0F;
    valid_ = float_literal(&parsed);
    if (valid_) { WriteValue(value_bytes.subspan(0, kFloatBytes), parsed); }
    break;
  }
  case PLYPropertyType::kDouble:
  default: {
    double parsed = 0.0;
    valid_ = double_literal(&parsed);
    if (valid_) { WriteValue(value_bytes.subspan(0, kDoubleBytes), parsed); }
    break;
  }
  }

  if (!valid_) { return false; }
  advance();

  switch (prop_type) {
  case PLYPropertyType::kChar:
    WriteValue(value_bytes.subspan(0, 1), static_cast<int8_t>(tmp_int));
    break;
  case PLYPropertyType::kUChar:
    value_bytes.front() = std::byte{ static_cast<uint8_t>(tmp_int) };
    break;
  case PLYPropertyType::kShort:
    WriteValue(value_bytes.subspan(0, 2), static_cast<int16_t>(tmp_int));
    break;
  case PLYPropertyType::kUShort:
    WriteValue(value_bytes.subspan(0, 2), static_cast<uint16_t>(tmp_int));
    break;
  default:
    break;
  }
  return true;
}


auto TriangulatePolygon(PolygonVertexCount vertex_count,
  std::span<const float> positions,
  MeshVertexCount mesh_vertex_count,
  PolygonIndices indices,
  TriangleDestination destination) -> uint32_t
{
  auto const count = static_cast<uint32_t>(vertex_count);
  if (count < kVerticesPerTriangle) { return 0U; }

  if (count == kVerticesPerTriangle) {
    SpanRef(destination.values, 0) = SpanAt(indices.values, 0);
    SpanRef(destination.values, 1) = SpanAt(indices.values, 1);
    SpanRef(destination.values, 2) = SpanAt(indices.values, 2);
    return 1U;
  }

  if (count == kQuadrilateralVertices) {
    SpanRef(destination.values, 0) = SpanAt(indices.values, 0);
    SpanRef(destination.values, 1) = SpanAt(indices.values, 1);
    SpanRef(destination.values, 2) = SpanAt(indices.values, 3);
    SpanRef(destination.values, 3) = SpanAt(indices.values, 2);
    SpanRef(destination.values, 4) = SpanAt(indices.values, 3);
    SpanRef(destination.values, kQuadrilateralIndices - 1U) = SpanAt(indices.values, 1);
    return 2U;
  }

  auto const mesh_verts = static_cast<uint32_t>(mesh_vertex_count);
  for (uint32_t index = 0; index < count; ++index) {
    if (SpanAt(indices.values, index) < 0 || std::cmp_greater_equal(SpanAt(indices.values, index), mesh_verts)) {
      return 0U;
    }
  }

  Vec3 const origin = VertexAt(positions, SpanAt(indices.values, 0));
  Vec3 const face_u = Normalize(VertexAt(positions, SpanAt(indices.values, 1)) - origin);
  Vec3 const face_normal =
    Normalize(Cross(face_u, Normalize(VertexAt(positions, SpanAt(indices.values, count - 1U)) - origin)));
  Vec3 const face_v = Normalize(Cross(face_normal, face_u));

  std::vector<Vec2> points_2d(count, Vec2{ .x = 0.0F, .y = 0.0F });
  for (uint32_t index = 1; index < count; ++index) {
    Vec3 const point = VertexAt(positions, SpanAt(indices.values, index)) - origin;
    points_2d.at(index) = Vec2{ .x = Dot(point, face_u), .y = Dot(point, face_v) };
  }

  std::vector<uint32_t> next(count, 0U);
  std::vector<uint32_t> prev(count, 0U);
  std::span<uint32_t> const next_span{ next };
  std::span<uint32_t> const prev_span{ prev };
  RingLinks const ring{ .prev = prev_span, .next = next_span };
  uint32_t first = 0U;
  for (uint32_t index = 0, prev_index = count - 1U; index < count; ++index) {
    next.at(prev_index) = index;
    prev.at(index) = prev_index;
    prev_index = index;
  }

  uint32_t remaining = count;
  size_t dst_idx = 0U;
  while (remaining > kVerticesPerTriangle) {
    uint32_t best_index = first;
    float best_angle = AngleAtVert(first, points_2d, ring);
    for (uint32_t index = SpanAt(next_span, first); index != first; index = SpanAt(next_span, index)) {
      float const angle = AngleAtVert(index, points_2d, ring);
      if (angle < best_angle) {
        best_index = index;
        best_angle = angle;
      }
    }

    uint32_t const next_index = SpanAt(next_span, best_index);
    uint32_t const prev_index = SpanAt(prev_span, best_index);

    SpanRef(destination.values, dst_idx++) = SpanAt(indices.values, best_index);
    SpanRef(destination.values, dst_idx++) = SpanAt(indices.values, next_index);
    SpanRef(destination.values, dst_idx++) = SpanAt(indices.values, prev_index);

    if (best_index == first) { first = next_index; }
    next.at(prev_index) = next_index;
    prev.at(next_index) = prev_index;
    --remaining;
  }

  SpanRef(destination.values, dst_idx++) = SpanAt(indices.values, first);
  SpanRef(destination.values, dst_idx++) = SpanAt(indices.values, SpanAt(next_span, first));
  SpanRef(destination.values, dst_idx++) = SpanAt(indices.values, SpanAt(prev_span, first));

  return count - 2U;
}

}// namespace miniply
