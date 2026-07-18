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
    { .name = "char", .type = PLYPropertyType::Char },
    { .name = "uchar", .type = PLYPropertyType::UChar },
    { .name = "short", .type = PLYPropertyType::Short },
    { .name = "ushort", .type = PLYPropertyType::UShort },
    { .name = "int", .type = PLYPropertyType::Int },
    { .name = "uint", .type = PLYPropertyType::UInt },
    { .name = "float", .type = PLYPropertyType::Float },
    { .name = "float32", .type = PLYPropertyType::Float },
    { .name = "float64", .type = PLYPropertyType::Double },
    { .name = "double", .type = PLYPropertyType::Double },
    { .name = "uint8", .type = PLYPropertyType::UChar },
    { .name = "uint16", .type = PLYPropertyType::UShort },
    { .name = "uint32", .type = PLYPropertyType::UInt },
    { .name = "int8", .type = PLYPropertyType::Char },
    { .name = "int16", .type = PLYPropertyType::Short },
    { .name = "int32", .type = PLYPropertyType::Int },
    { .name = "", .type = PLYPropertyType::None },
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

  [[nodiscard]] constexpr double double_digit(char character) { return static_cast<double>(character - '0'); }

  [[nodiscard]] Vec2 operator-(Vec2 lhs, Vec2 rhs) { return Vec2{ .x = lhs.x - rhs.x, .y = lhs.y - rhs.y }; }

  [[nodiscard]] float dot(Vec2 lhs, Vec2 rhs) { return (lhs.x * rhs.x) + (lhs.y * rhs.y); }

  [[nodiscard]] float length(Vec2 vec) { return std::sqrt(dot(vec, vec)); }

  [[nodiscard]] Vec2 normalize(Vec2 vec)
  {
    float const len = length(vec);
    return Vec2{ .x = vec.x / len, .y = vec.y / len };
  }

  [[nodiscard]] Vec3 operator-(Vec3 lhs, Vec3 rhs)
  { return Vec3{ .x = lhs.x - rhs.x, .y = lhs.y - rhs.y, .z = lhs.z - rhs.z }; }

  [[nodiscard]] float dot(Vec3 lhs, Vec3 rhs) { return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z); }

  [[nodiscard]] float length(Vec3 vec) { return std::sqrt(dot(vec, vec)); }

  [[nodiscard]] Vec3 normalize(Vec3 vec)
  {
    float const len = length(vec);
    return Vec3{ .x = vec.x / len, .y = vec.y / len, .z = vec.z / len };
  }

  [[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs)
  {
    return Vec3{ .x = (lhs.y * rhs.z) - (lhs.z * rhs.y),
      .y = (lhs.z * rhs.x) - (lhs.x * rhs.z),
      .z = (lhs.x * rhs.y) - (lhs.y * rhs.x) };
  }

  [[nodiscard]] bool is_whitespace(char character)
  { return character == ' ' || character == '\t' || character == '\r'; }

  [[nodiscard]] bool is_digit(char character) { return character >= '0' && character <= '9'; }

  [[nodiscard]] char to_lower_ascii(char character)
  { return static_cast<char>(static_cast<unsigned char>(character) | kAsciiToLowerBit); }


  [[nodiscard]] char span_char(std::span<const char> buffer, size_t index) { return buffer.subspan(index, 1U).front(); }

  template<typename T> [[nodiscard]] T span_at(std::span<T> span, size_t index)
  { return span.subspan(index, 1U).front(); }

  template<typename T> [[nodiscard]] T &span_ref(std::span<T> span, size_t index)
  { return *std::next(span.begin(), static_cast<std::ptrdiff_t>(index)); }

  template<typename T> [[nodiscard]] T span_at(std::span<const T> span, size_t index)
  { return span.subspan(index, 1U).front(); }

  using FileHandle = std::unique_ptr<FILE, decltype(&fclose)>;

  [[nodiscard]] std::span<std::byte> byte_span(void *dest, size_t size)
  { return { static_cast<std::byte *>(dest), size }; }

  [[nodiscard]] bool is_letter(char character)
  {
    char const lower = to_lower_ascii(character);
    return lower >= 'a' && lower <= 'z';
  }

  [[nodiscard]] bool is_alnum(char character) { return is_digit(character) || is_letter(character); }

  [[nodiscard]] bool is_keyword_start(char character) { return is_letter(character) || character == '_'; }

  [[nodiscard]] bool is_keyword_part(char character) { return is_alnum(character) || character == '_'; }

  [[nodiscard]] bool is_safe_buffer_end(char character)
  { return (character > 0 && character <= kAsciiControlMax) || (character >= kAsciiDelete); }

  [[nodiscard]] FileHandle open_file(const char *filename, const char *mode)
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

  [[nodiscard]] bool file_seek(FILE *file, int64_t offset, int origin)
  {
#ifdef _WIN32
    return _fseeki64(file, offset, origin) == 0;
#else
    return std::fseek(file, offset, origin) == 0;
#endif
  }

  template<typename T> [[nodiscard]] T read_value(std::span<const std::byte> bytes)
  {
    std::array<std::byte, sizeof(T)> storage{};
    std::memcpy(storage.data(), bytes.data(), sizeof(T));
    return std::bit_cast<T>(storage);
  }

  template<typename T> void write_value(std::span<std::byte> bytes, T value)
  {
    auto const storage = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    std::memcpy(bytes.data(), storage.data(), sizeof(T));
  }

  void endian_swap_2(std::span<std::byte, 2> data)
  {
    auto tmp = read_value<uint16_t>(data);
    auto const unsignedTmp = static_cast<uint16_t>(tmp);
    tmp = static_cast<uint16_t>(
      static_cast<uint16_t>(unsignedTmp >> kBitsPerByte) | static_cast<uint16_t>(unsignedTmp << kBitsPerByte));
    write_value(data, tmp);
  }

  void endian_swap_4(std::span<std::byte, 4> data)
  {
    auto tmp = read_value<uint32_t>(data);
    tmp = (tmp >> kEndianSwapHalf32) | (tmp << kEndianSwapHalf32);
    tmp = ((tmp & kEndianMask8In32) >> kBitsPerByte) | ((tmp & kEndianMask8Low32) << kBitsPerByte);
    write_value(data, tmp);
  }

  void endian_swap_8(std::span<std::byte, kScalarValueBytes> data)
  {
    auto tmp = read_value<uint64_t>(data);
    tmp = (tmp >> kEndianSwapHalf64) | (tmp << kEndianSwapHalf64);
    tmp = ((tmp & kEndianMask16In64) >> kEndianSwapQuarter64) | ((tmp & kEndianMask16Low64) << kEndianSwapQuarter64);
    tmp = ((tmp & kEndianMask8In64) >> kBitsPerByte) | ((tmp & kEndianMask8Low64) << kBitsPerByte);
    write_value(data, tmp);
  }

  void endian_swap(std::span<std::byte> data, PLYPropertyType type)
  {
    switch (kPLYPropertySize.at(static_cast<size_t>(type))) {
    case 2U:
      endian_swap_2(data.subspan<0, 2>());
      break;
    case 4U:
      endian_swap_4(data.subspan<0, 4>());
      break;
    case kScalarValueBytes:
      endian_swap_8(data.subspan<0, kScalarValueBytes>());
      break;
    default:
      break;
    }
  }

  void endian_swap_array(std::span<std::byte> data, PLYPropertyType type, int count)
  {
    size_t const elementSize = kPLYPropertySize.at(static_cast<size_t>(type));
    for (int index = 0; index < count; ++index) {
      endian_swap(data.subspan(static_cast<size_t>(index) * elementSize, elementSize), type);
    }
  }

  template<class T> void copy_and_convert_to(T *dest, std::span<const std::byte> src, PLYPropertyType srcType)
  {
    switch (srcType) {
    case PLYPropertyType::Char:
      *dest = static_cast<T>(static_cast<unsigned char>(read_value<int8_t>(src)));
      break;
    case PLYPropertyType::UChar:
      *dest = static_cast<T>(std::to_integer<uint8_t>(src.front()));
      break;
    case PLYPropertyType::Short:
      *dest = static_cast<T>(read_value<int16_t>(src));
      break;
    case PLYPropertyType::UShort:
      *dest = static_cast<T>(read_value<uint16_t>(src));
      break;
    case PLYPropertyType::Int:
      *dest = static_cast<T>(read_value<int32_t>(src));
      break;
    case PLYPropertyType::UInt:
      *dest = static_cast<T>(read_value<uint32_t>(src));
      break;
    case PLYPropertyType::Float:
      *dest = static_cast<T>(read_value<float>(src));
      break;
    case PLYPropertyType::Double:
      *dest = static_cast<T>(read_value<double>(src));
      break;
    case PLYPropertyType::None:
      break;
    }
  }

  void copy_and_convert(std::span<std::byte> dest,
    PLYPropertyType destType,
    std::span<const std::byte> src,
    PLYPropertyType srcType)
  {
    switch (destType) {
    case PLYPropertyType::Char: {
      int8_t value = 0;
      copy_and_convert_to(&value, src, srcType);
      write_value(dest.subspan(0, 1), value);
      break;
    }
    case PLYPropertyType::UChar: {
      uint8_t value = 0;
      copy_and_convert_to(&value, src, srcType);
      dest.front() = std::byte{ value };
      break;
    }
    case PLYPropertyType::Short: {
      int16_t value = 0;
      copy_and_convert_to(&value, src, srcType);
      write_value(dest.subspan(0, 2), value);
      break;
    }
    case PLYPropertyType::UShort: {
      uint16_t value = 0;
      copy_and_convert_to(&value, src, srcType);
      write_value(dest.subspan(0, 2), value);
      break;
    }
    case PLYPropertyType::Int: {
      int32_t value = 0;
      copy_and_convert_to(&value, src, srcType);
      write_value(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::UInt: {
      uint32_t value = 0;
      copy_and_convert_to(&value, src, srcType);
      write_value(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::Float: {
      float value = 0.0F;
      copy_and_convert_to(&value, src, srcType);
      write_value(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::Double: {
      double value = 0.0;
      copy_and_convert_to(&value, src, srcType);
      write_value(dest.subspan(0, kDoubleBytes), value);
      break;
    }
    case PLYPropertyType::None:
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

  [[nodiscard]] bool compatible_types(PLYPropertyType srcType, PLYPropertyType destType)
  {
    TypePair const types{ .src = srcType, .dest = destType };
    return (types.src == types.dest)
           || (types.src < PLYPropertyType::Float
               && (static_cast<uint32_t>(types.src) ^ kSignedPairToggle) == static_cast<uint32_t>(types.dest));
  }

  struct DoubleParseState
  {
    size_t pos = 0;
    double value = 0.0;
    bool hasIntDigits = false;
    bool hasFracDigits = false;
  };

  [[nodiscard]] bool int_literal(std::span<const char> buffer, size_t startPos, size_t &endPos, int *val)
  {
    if (startPos >= buffer.size()) { return false; }
    size_t pos = startPos;

    bool negative = false;
    if (span_char(buffer, pos) == '-') {
      negative = true;
      ++pos;
    } else if (pos < buffer.size() && span_char(buffer, pos) == '+') {
      ++pos;
    }

    bool const hasLeadingZeroes = pos < buffer.size() && span_char(buffer, pos) == '0';
    if (hasLeadingZeroes) {
      while (pos < buffer.size() && span_char(buffer, pos) == '0') { ++pos; }
    }

    int numDigits = 0;
    int localVal = 0;
    while (pos < buffer.size() && is_digit(span_char(buffer, pos))) {
      localVal = (localVal * kDecimalRadix) + static_cast<int>(span_char(buffer, pos) - '0');
      ++numDigits;
      ++pos;
    }

    if (numDigits == 0 && hasLeadingZeroes) { numDigits = 1; }

    if (numDigits == 0 || pos >= buffer.size() || is_letter(span_char(buffer, pos)) || span_char(buffer, pos) == '_') {
      return false;
    }
    if (numDigits > kMaxIntDigits) { return false; }

    if (val != nullptr) { *val = negative ? -localVal : localVal; }
    endPos = pos;
    return true;
  }

  [[nodiscard]] bool parse_double_integer_part(std::span<const char> buffer, DoubleParseState &state)
  {
    state.hasIntDigits = state.pos < buffer.size() && is_digit(span_char(buffer, state.pos));
    if (state.hasIntDigits) {
      while (state.pos < buffer.size() && is_digit(span_char(buffer, state.pos))) {
        state.value = (state.value * kDecimalBase) + double_digit(span_char(buffer, state.pos));
        ++state.pos;
      }
      return true;
    }
    if (state.pos >= buffer.size() || span_char(buffer, state.pos) != '.') { return false; }
    return true;
  }

  [[nodiscard]] bool parse_double_fraction_part(std::span<const char> buffer, DoubleParseState &state)
  {
    if (state.pos >= buffer.size() || span_char(buffer, state.pos) != '.') { return true; }

    ++state.pos;
    state.hasFracDigits = state.pos < buffer.size() && is_digit(span_char(buffer, state.pos));
    if (!state.hasFracDigits) { return state.hasIntDigits; }

    double scale = kFractionScale;
    while (state.pos < buffer.size() && is_digit(span_char(buffer, state.pos))) {
      state.value += scale * double_digit(span_char(buffer, state.pos));
      scale *= kFractionScale;
      ++state.pos;
    }
    return true;
  }

  [[nodiscard]] bool parse_double_exponent(std::span<const char> buffer, DoubleParseState &state, bool applyExponent)
  {
    if (state.pos >= buffer.size() || (span_char(buffer, state.pos) != 'e' && span_char(buffer, state.pos) != 'E')) {
      return true;
    }

    ++state.pos;
    bool negativeExponent = false;
    if (state.pos < buffer.size() && span_char(buffer, state.pos) == '-') {
      negativeExponent = true;
      ++state.pos;
    } else if (state.pos < buffer.size() && span_char(buffer, state.pos) == '+') {
      ++state.pos;
    }

    if (state.pos >= buffer.size() || !is_digit(span_char(buffer, state.pos))) { return false; }

    double exponent = 0.0;
    while (state.pos < buffer.size() && is_digit(span_char(buffer, state.pos))) {
      exponent = (exponent * kDecimalBase) + double_digit(span_char(buffer, state.pos));
      ++state.pos;
    }

    if (applyExponent) {
      if (negativeExponent) { exponent = -exponent; }
      state.value *= std::pow(kDecimalBase, exponent);
    }
    return true;
  }

  [[nodiscard]] bool double_literal(std::span<const char> buffer, size_t startPos, size_t &endPos, double *val)
  {
    if (startPos >= buffer.size()) { return false; }

    DoubleParseState state{};
    state.pos = startPos;

    bool negative = false;
    if (span_char(buffer, state.pos) == '-') {
      negative = true;
      ++state.pos;
    } else if (state.pos < buffer.size() && span_char(buffer, state.pos) == '+') {
      ++state.pos;
    }

    if (!parse_double_integer_part(buffer, state)) { return false; }
    if (!parse_double_fraction_part(buffer, state)) { return false; }
    if (!parse_double_exponent(buffer, state, val != nullptr)) { return false; }

    if (state.pos < buffer.size()
        && (span_char(buffer, state.pos) == '.' || span_char(buffer, state.pos) == '_'
            || is_alnum(span_char(buffer, state.pos)))) {
      return false;
    }

    if (negative) { state.value = -state.value; }

    if (val != nullptr) { *val = state.value; }
    endPos = state.pos;
    return true;
  }

  [[nodiscard]] bool float_literal(std::span<const char> buffer, size_t startPos, size_t &endPos, float *val)
  {
    double parsed = 0.0;
    bool const success = double_literal(buffer, startPos, endPos, &parsed);
    if (success && val != nullptr) { *val = static_cast<float>(parsed); }
    return success;
  }

  [[nodiscard]] Vec3 vertex_at(std::span<const float> pos, int index)
  {
    size_t const base = static_cast<size_t>(index) * kVerticesPerTriangle;
    return Vec3{ .x = span_at(pos, base), .y = span_at(pos, base + 1U), .z = span_at(pos, base + 2U) };
  }

  struct PropertyLayoutInfo
  {
    bool contiguousCols = true;
    bool contiguousRows = false;
    bool conversionRequired = false;
    uint32_t expectedOffset = 0;
  };

  [[nodiscard]] bool validate_property_indices(PLYElement const &elem, std::span<const uint32_t> propIdxs)
  {
    return std::ranges::all_of(propIdxs, [&](uint32_t const propIdx) { return propIdx < elem.properties.size(); });
  }

  [[nodiscard]] PropertyLayoutInfo
    analyze_property_layout(PLYElement const &elem, std::span<const uint32_t> propIdxs, PLYPropertyType destType)
  {
    PropertyLayoutInfo info{};
    info.expectedOffset = elem.properties.at(propIdxs.front()).offset;
    for (uint32_t const propIdx : propIdxs) {
      PLYProperty const &prop = elem.properties.at(propIdx);
      if (prop.offset != info.expectedOffset) {
        info.contiguousCols = false;
        break;
      }
      info.expectedOffset = prop.offset + kPLYPropertySize.at(static_cast<size_t>(prop.type));
    }

    info.contiguousRows = info.contiguousCols && (elem.properties.at(propIdxs.front()).offset == 0U)
                          && (info.expectedOffset == elem.rowStride);

    for (uint32_t const propIdx : propIdxs) {
      PLYProperty const &prop = elem.properties.at(propIdx);
      if (!compatible_types(prop.type, destType)) {
        info.conversionRequired = true;
        break;
      }
    }
    return info;
  }

  void extract_contiguous_rows(std::span<const uint8_t> elementData, size_t numBytes, std::span<std::byte> dest)
  { std::memcpy(dest.data(), elementData.data(), numBytes); }

  void extract_contiguous_columns(std::span<const uint8_t> elementData,
    PLYElement const &elem,
    std::span<const uint32_t> propIdxs,
    uint32_t expectedOffset,
    std::span<std::byte> dest)
  {
    size_t const numBytes = static_cast<size_t>(expectedOffset) - elem.properties.at(propIdxs.front()).offset;
    size_t destOffset = 0;
    size_t fromOffset = elem.properties.at(propIdxs.front()).offset;
    while (fromOffset < elementData.size()) {
      std::memcpy(
        dest.subspan(destOffset, numBytes).data(), elementData.subspan(fromOffset, numBytes).data(), numBytes);
      fromOffset += elem.rowStride;
      destOffset += numBytes;
    }
  }

  void extract_scattered_columns(std::span<const uint8_t> elementData,
    PLYElement const &elem,
    std::span<const uint32_t> propIdxs,
    PLYPropertyType destType,
    std::span<std::byte> dest)
  {
    size_t const colBytes = kPLYPropertySize.at(static_cast<size_t>(destType));
    size_t destOffset = 0;
    size_t rowOffset = 0;
    while (rowOffset < elementData.size()) {
      for (uint32_t const propIdx : propIdxs) {
        PLYProperty const &prop = elem.properties.at(propIdx);
        std::memcpy(dest.subspan(destOffset, colBytes).data(),
          elementData.subspan(rowOffset + prop.offset, colBytes).data(),
          colBytes);
        destOffset += colBytes;
      }
      rowOffset += elem.rowStride;
    }
  }

  void convert_scattered_columns(std::span<const uint8_t> elementData,
    PLYElement const &elem,
    std::span<const uint32_t> propIdxs,
    PLYPropertyType destType,
    std::span<std::byte> dest)
  {
    size_t const colBytes = kPLYPropertySize.at(static_cast<size_t>(destType));
    size_t destOffset = 0;
    size_t rowOffset = 0;
    while (rowOffset < elementData.size()) {
      for (uint32_t const propIdx : propIdxs) {
        PLYProperty const &prop = elem.properties.at(propIdx);
        copy_and_convert(dest.subspan(destOffset, colBytes),
          destType,
          std::as_bytes(elementData.subspan(rowOffset + prop.offset, colBytes)),
          prop.type);
        destOffset += colBytes;
      }
      rowOffset += elem.rowStride;
    }
  }

  [[nodiscard]] float angle_at_vert(uint32_t idx, std::span<const Vec2> points2D, RingLinks ring)
  {
    Vec2 const xaxis = normalize(span_at(points2D, span_at(ring.next, idx)) - span_at(points2D, idx));
    Vec2 const yaxis = Vec2{ .x = -xaxis.y, .y = xaxis.x };
    Vec2 const p2p0 = span_at(points2D, span_at(ring.prev, idx)) - span_at(points2D, idx);
    float angle = std::atan2(dot(p2p0, yaxis), dot(p2p0, xaxis));
    if (angle <= 0.0F || angle >= kPi) { angle = kReflexAngle; }
    return angle;
  }

}// namespace

//
// PLYElement methods
//

void PLYElement::calculate_offsets()
{
  fixedSize =
    !std::ranges::any_of(properties, [](PLYProperty const &prop) { return prop.countType != PLYPropertyType::None; });

  rowStride = 0;
  for (PLYProperty &prop : properties) {
    if (prop.countType != PLYPropertyType::None) { continue; }
    prop.offset = rowStride;
    rowStride += kPLYPropertySize.at(static_cast<size_t>(prop.type));
  }
}


uint32_t PLYElement::find_property(const char *propName) const
{
  for (uint32_t index = 0; index < static_cast<uint32_t>(properties.size()); ++index) {
    if (strcmp(propName, properties.at(index).name.c_str()) == 0) { return index; }
  }
  return kInvalidIndex;
}


bool PLYElement::find_properties(std::span<uint32_t> propIdxs, std::span<const char *const> propNames) const
{
  if (propIdxs.size() != propNames.size()) { return false; }
  for (size_t index = 0; index < propNames.size(); ++index) {
    span_ref(propIdxs, index) = find_property(span_at(propNames, index));
    if (span_ref(propIdxs, index) == kInvalidIndex) { return false; }
  }
  return true;
}


bool PLYElement::convert_list_to_fixed_size(ListPropertyIndex listPropIdx,
  FixedListSize listSize,
  std::span<uint32_t> newPropIdxs)
{
  auto const listPropIndex = static_cast<uint32_t>(listPropIdx);
  auto const fixedListSize = static_cast<uint32_t>(listSize);
  if (fixedSize || listPropIndex >= properties.size()
      || properties.at(listPropIndex).countType == PLYPropertyType::None) {
    return false;
  }
  if (newPropIdxs.size() < fixedListSize) { return false; }

  PLYProperty const oldListProp = properties.at(listPropIndex);

  PLYProperty &countProp = properties.at(listPropIndex);
  countProp.name = std::format("{}_count", oldListProp.name);
  countProp.type = oldListProp.countType;
  countProp.countType = PLYPropertyType::None;
  countProp.stride = kPLYPropertySize.at(static_cast<size_t>(oldListProp.countType));

  if (fixedListSize > 0U) {
    if (listPropIndex + 1U == static_cast<uint32_t>(properties.size())) {
      properties.resize(properties.size() + fixedListSize);
    } else {
      size_t const insertIndex = static_cast<size_t>(listPropIndex) + 1U;
      properties.insert(properties.begin() + static_cast<std::vector<PLYProperty>::difference_type>(insertIndex),
        fixedListSize,
        PLYProperty{});
    }

    for (uint32_t itemIndex = 0; itemIndex < fixedListSize; ++itemIndex) {
      uint32_t const propIdx = listPropIndex + 1U + itemIndex;
      PLYProperty &itemProp = properties.at(propIdx);
      itemProp.name = std::format("{}_{}", oldListProp.name, itemIndex);
      itemProp.type = oldListProp.type;
      itemProp.countType = PLYPropertyType::None;
      itemProp.stride = kPLYPropertySize.at(static_cast<size_t>(oldListProp.type));
      span_ref(newPropIdxs, itemIndex) = propIdx;
    }
  }

  calculate_offsets();
  return true;
}


//
// PLYReader methods
//

PLYReader::PLYReader(const char *filename)
  : m_file(open_file(filename, "rb")), m_buf(static_cast<size_t>(kPLYReadBufferSize) + 1U, '\0'),
    m_tmpBuf(static_cast<size_t>(kPLYTempBufferSize) + 1U, '\0'), m_pos(static_cast<size_t>(kPLYReadBufferSize)),
    m_end(static_cast<size_t>(kPLYReadBufferSize)), m_bufDataEnd(static_cast<size_t>(kPLYReadBufferSize))
{
  if (m_file == nullptr) {
    m_valid = false;
    return;
  }
  m_valid = true;

  refill_buffer();

  m_valid = keyword("ply") && next_line() && keyword("format") && advance() && typed_which(kPLYFileTypes, &m_fileType)
            && advance() && int_literal(&m_majorVersion) && advance() && match(".") && advance()
            && int_literal(&m_minorVersion) && next_line() && parse_elements() && keyword("end_header") && advance()
            && match("\n") && accept();
  if (!m_valid) { return; }
  m_inDataSection = true;
  if (m_fileType == PLYFileType::ASCII) { advance(); }

  for (PLYElement &elem : m_elements) { elem.calculate_offsets(); }
}


PLYReader::~PLYReader() = default;


bool PLYReader::valid() const { return m_valid; }


bool PLYReader::has_element() const { return m_valid && m_currentElement < m_elements.size(); }


const PLYElement *PLYReader::element() const
{
  assert(has_element());
  return &m_elements.at(m_currentElement);
}


bool PLYReader::load_element()
{
  assert(has_element());
  if (m_elementLoaded) { return true; }

  PLYElement &elem = m_elements.at(m_currentElement);
  return elem.fixedSize ? load_fixed_size_element(elem) : load_variable_size_element(elem);
}


char PLYReader::char_at(size_t index) const { return m_buf.at(index); }


bool PLYReader::ensure_bytes_available(size_t numBytes)
{
  if (m_pos + numBytes > m_bufDataEnd) {
    if (!refill_buffer() || m_pos + numBytes > m_bufDataEnd) {
      m_valid = false;
      return false;
    }
  }
  return true;
}


void PLYReader::clear_list_property_storage(PLYElement &elem)
{
  for (PLYProperty &prop : elem.properties) {
    if (prop.countType == PLYPropertyType::None) { continue; }
    prop.listData.clear();
    prop.listData.shrink_to_fit();
    prop.rowCount.clear();
    prop.rowCount.shrink_to_fit();
  }
  m_elementData.clear();
  m_elementLoaded = false;
}


void PLYReader::skip_unloaded_ascii_element(PLYElement const &elem)
{
  for (uint32_t row = 0; row < elem.count; ++row) { next_line(); }
}


void PLYReader::skip_unloaded_binary_fixed_element(PLYElement const &elem)
{
  auto const elementStart = static_cast<int64_t>(m_pos);
  int64_t const elementSize = static_cast<int64_t>(elem.rowStride) * static_cast<int64_t>(elem.count);
  int64_t const elementEnd = elementStart + elementSize;
  if (std::cmp_greater_equal(elementEnd, static_cast<int64_t>(kPLYReadBufferSize))) {
    m_bufOffset += elementEnd;
    if (!file_seek(m_file.get(), m_bufOffset, SEEK_SET)) {
      m_valid = false;
      return;
    }
    m_bufDataEnd = static_cast<size_t>(kPLYReadBufferSize);
    m_pos = m_bufDataEnd;
    m_end = m_bufDataEnd;
    refill_buffer();
  } else {
    m_pos = static_cast<size_t>(elementEnd);
    m_end = m_pos;
  }
}


void PLYReader::skip_unloaded_binary_variable_element(PLYElement const &elem)
{
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty const &prop : elem.properties) {
      if (prop.countType == PLYPropertyType::None) {
        uint32_t const numBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
        if (!ensure_bytes_available(numBytes)) { return; }
        m_pos += numBytes;
        m_end = m_pos;
        continue;
      }

      uint32_t numBytes = kPLYPropertySize.at(static_cast<size_t>(prop.countType));
      if (!ensure_bytes_available(numBytes)) { return; }

      int count = 0;
      copy_and_convert_to(&count, std::as_bytes(std::span{ m_buf }.subspan(m_pos, numBytes)), prop.countType);
      if (count < 0) {
        m_valid = false;
        return;
      }

      numBytes += static_cast<uint32_t>(count) * kPLYPropertySize.at(static_cast<size_t>(prop.type));
      if (!ensure_bytes_available(numBytes)) { return; }
      m_pos += numBytes;
      m_end = m_pos;
    }
  }
}


void PLYReader::skip_unloaded_binary_big_endian_variable_element(PLYElement const &elem)
{
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty const &prop : elem.properties) {
      if (prop.countType == PLYPropertyType::None) {
        uint32_t const numBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
        if (!ensure_bytes_available(numBytes)) { return; }
        m_pos += numBytes;
        m_end = m_pos;
        continue;
      }

      uint32_t numBytes = kPLYPropertySize.at(static_cast<size_t>(prop.countType));
      if (!ensure_bytes_available(numBytes)) { return; }

      int count = 0;
      std::array<std::byte, kScalarValueBytes> tmp{};
      std::memcpy(tmp.data(), std::span{ m_buf }.subspan(m_pos, numBytes).data(), numBytes);
      endian_swap(std::span{ tmp }.subspan(0, numBytes), prop.countType);
      copy_and_convert_to(&count, std::span<const std::byte>{ tmp }.subspan(0, numBytes), prop.countType);
      if (count < 0) {
        m_valid = false;
        return;
      }

      numBytes += static_cast<uint32_t>(count) * kPLYPropertySize.at(static_cast<size_t>(prop.type));
      if (!ensure_bytes_available(numBytes)) { return; }
      m_pos += numBytes;
      m_end = m_pos;
    }
  }
}


void PLYReader::next_element()
{
  if (!has_element()) { return; }

  PLYElement &elem = m_elements.at(m_currentElement);
  m_currentElement++;

  if (m_elementLoaded) {
    clear_list_property_storage(elem);
    return;
  }

  if (m_fileType == PLYFileType::ASCII) {
    skip_unloaded_ascii_element(elem);
  } else if (elem.fixedSize) {
    skip_unloaded_binary_fixed_element(elem);
  } else if (m_fileType == PLYFileType::Binary) {
    skip_unloaded_binary_variable_element(elem);
  } else {
    skip_unloaded_binary_big_endian_variable_element(elem);
  }
}


PLYFileType PLYReader::file_type() const { return m_fileType; }


int PLYReader::version_major() const { return m_majorVersion; }


int PLYReader::version_minor() const { return m_minorVersion; }


uint32_t PLYReader::num_elements() const { return m_valid ? static_cast<uint32_t>(m_elements.size()) : 0U; }


uint32_t PLYReader::find_element(const char *name) const
{
  for (uint32_t index = 0; index < num_elements(); ++index) {
    if (strcmp(m_elements.at(index).name.c_str(), name) == 0) { return index; }
  }
  return kInvalidIndex;
}


PLYElement *PLYReader::get_element(uint32_t idx) { return (idx < num_elements()) ? &m_elements.at(idx) : nullptr; }


bool PLYReader::element_is(const char *name) const
{ return has_element() && strcmp(element()->name.c_str(), name) == 0; }


uint32_t PLYReader::num_rows() const { return has_element() ? element()->count : 0U; }


uint32_t PLYReader::find_property(const char *name) const
{ return has_element() ? element()->find_property(name) : kInvalidIndex; }


bool PLYReader::find_properties(std::span<uint32_t> propIdxs, std::span<const char *const> propNames) const
{
  if (!has_element()) { return false; }
  return element()->find_properties(propIdxs, propNames);
}


bool PLYReader::extract_properties(std::span<const uint32_t> propIdxs, PLYPropertyType destType, void *dest) const
{
  if (propIdxs.empty() || dest == nullptr) { return false; }

  PLYElement const *elem = element();
  if (!validate_property_indices(*elem, propIdxs)) { return false; }

  PropertyLayoutInfo const layout = analyze_property_layout(*elem, propIdxs, destType);
  auto destBytes = std::span{ static_cast<std::byte *>(dest), m_elementData.size() };

  if (!layout.conversionRequired) {
    if (layout.contiguousRows) {
      extract_contiguous_rows(m_elementData, m_elementData.size(), destBytes);
    } else if (layout.contiguousCols) {
      extract_contiguous_columns(m_elementData, *elem, propIdxs, layout.expectedOffset, destBytes);
    } else {
      extract_scattered_columns(m_elementData, *elem, propIdxs, destType, destBytes);
    }
  } else {
    convert_scattered_columns(m_elementData, *elem, propIdxs, destType, destBytes);
  }

  return true;
}


bool PLYReader::extract_properties_with_stride(std::span<const uint32_t> propIdxs,
  PLYPropertyType destType,
  void *dest,
  uint32_t destStride) const
{
  if (propIdxs.empty() || dest == nullptr) { return false; }

  uint32_t const minDestStride =
    static_cast<uint32_t>(propIdxs.size()) * kPLYPropertySize.at(static_cast<size_t>(destType));
  if (destStride == 0U || destStride == minDestStride) { return extract_properties(propIdxs, destType, dest); }
  if (destStride < minDestStride) { return false; }

  PLYElement const *elem = element();
  if (!validate_property_indices(*elem, propIdxs)) { return false; }

  PropertyLayoutInfo const layout = analyze_property_layout(*elem, propIdxs, destType);
  size_t const colBytes = kPLYPropertySize.at(static_cast<size_t>(destType));
  size_t const colPadding = static_cast<size_t>(destStride) - static_cast<size_t>(minDestStride);
  size_t destOffset = 0;
  size_t rowOffset = 0;

  while (rowOffset < m_elementData.size()) {
    if (!layout.conversionRequired && layout.contiguousCols) {
      size_t const numBytes = static_cast<size_t>(layout.expectedOffset) - elem->properties.at(propIdxs.front()).offset;
      std::memcpy(byte_span(dest, m_elementData.size()).subspan(destOffset, numBytes).data(),
        std::span{ m_elementData }.subspan(rowOffset + elem->properties.at(propIdxs.front()).offset, numBytes).data(),
        numBytes);
    } else {
      for (uint32_t const propIdx : propIdxs) {
        PLYProperty const &prop = elem->properties.at(propIdx);
        auto destSpan = byte_span(dest, m_elementData.size()).subspan(destOffset, colBytes);
        auto srcSpan = std::as_bytes(std::span{ m_elementData }.subspan(rowOffset + prop.offset, colBytes));
        if (layout.conversionRequired) {
          copy_and_convert(destSpan, destType, srcSpan, prop.type);
        } else {
          std::memcpy(destSpan.data(), srcSpan.data(), colBytes);
        }
        destOffset += colBytes;
      }
      destOffset += colPadding;
      rowOffset += elem->rowStride;
      continue;
    }
    destOffset += static_cast<size_t>(destStride);
    rowOffset += elem->rowStride;
  }

  return true;
}


const uint32_t *PLYReader::get_list_counts(uint32_t propIdx) const
{
  if (!has_element() || propIdx >= element()->properties.size()
      || element()->properties.at(propIdx).countType == PLYPropertyType::None) {
    return nullptr;
  }
  return element()->properties.at(propIdx).rowCount.data();
}


uint32_t PLYReader::sum_of_list_counts(uint32_t propIdx) const
{
  if (!has_element() || propIdx >= element()->properties.size()
      || element()->properties.at(propIdx).countType == PLYPropertyType::None) {
    return 0U;
  }
  PLYProperty const &prop = element()->properties.at(propIdx);
  return static_cast<uint32_t>(prop.listData.size() / kPLYPropertySize.at(static_cast<size_t>(prop.type)));
}


const uint8_t *PLYReader::get_list_data(uint32_t propIdx) const
{
  if (!has_element() || propIdx >= element()->properties.size()
      || element()->properties.at(propIdx).countType == PLYPropertyType::None) {
    return nullptr;
  }
  return element()->properties.at(propIdx).listData.data();
}


bool PLYReader::extract_list_property(uint32_t propIdx, PLYPropertyType destType, void *dest) const
{
  if (!has_element() || propIdx >= element()->properties.size()
      || element()->properties.at(propIdx).countType == PLYPropertyType::None || dest == nullptr) {
    return false;
  }

  PLYProperty const &prop = element()->properties.at(propIdx);
  if (compatible_types(prop.type, destType)) {
    std::memcpy(dest, prop.listData.data(), prop.listData.size());
    return true;
  }

  size_t destOffset = 0;
  size_t fromOffset = 0;
  size_t const toBytes = kPLYPropertySize.at(static_cast<size_t>(destType));
  size_t const fromBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  while (fromOffset < prop.listData.size()) {
    copy_and_convert(byte_span(dest, prop.listData.size()).subspan(destOffset, toBytes),
      destType,
      std::as_bytes(std::span{ prop.listData }.subspan(fromOffset, fromBytes)),
      prop.type);
    destOffset += toBytes;
    fromOffset += fromBytes;
  }

  return true;
}


uint32_t PLYReader::num_triangles(uint32_t propIdx) const
{
  uint32_t const *counts = get_list_counts(propIdx);
  if (counts == nullptr) { return 0U; }

  std::span<const uint32_t> const countSpan{ counts, element()->count };
  uint32_t num = 0U;
  for (uint32_t row = 0; row < element()->count; ++row) {
    if (span_at(countSpan, row) >= kVerticesPerTriangle) { num += span_at(countSpan, row) - 2U; }
  }
  return num;
}


bool PLYReader::requires_triangulation(uint32_t propIdx) const
{
  uint32_t const *counts = get_list_counts(propIdx);
  if (counts == nullptr) { return false; }

  std::span<const uint32_t> const countSpan{ counts, element()->count };
  for (uint32_t row = 0; row < element()->count; ++row) {
    if (span_at(countSpan, row) != kVerticesPerTriangle) { return true; }
  }
  return false;
}


bool PLYReader::extract_triangles(uint32_t propIdx,
  std::span<const float> positions,
  MeshVertexCount meshVertexCount,
  PLYPropertyType destType,
  void *dest) const
{
  if (dest == nullptr) { return false; }
  if (!requires_triangulation(propIdx)) { return extract_list_property(propIdx, destType, dest); }

  PLYProperty const &prop = element()->properties.at(propIdx);
  bool const convertSrc = !compatible_types(prop.type, PLYPropertyType::Int);
  bool const convertDst = !compatible_types(PLYPropertyType::Int, destType);

  if (convertSrc && convertDst) {
    return extract_triangles_convert_both(propIdx, positions, meshVertexCount, destType, dest);
  }
  if (convertSrc) { return extract_triangles_convert_src(propIdx, positions, meshVertexCount, destType, dest); }
  if (convertDst) { return extract_triangles_convert_dst(propIdx, positions, meshVertexCount, destType, dest); }
  return extract_triangles_native(propIdx, positions, meshVertexCount, destType, dest);
}


bool PLYReader::extract_triangles_convert_both(uint32_t propIdx,
  std::span<const float> positions,
  MeshVertexCount meshVertexCount,
  PLYPropertyType destType,
  void *dest) const
{
  PLYElement const *elem = element();
  PLYProperty const &prop = elem->properties.at(propIdx);
  std::span<const uint32_t> const counts = prop.rowCount;
  std::span<const uint8_t> const data = prop.listData;
  size_t destByteOffset = 0;
  size_t const srcValBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const destValBytes = kPLYPropertySize.at(static_cast<size_t>(destType));
  size_t faceOffset = 0;
  auto destBytes = byte_span(dest, data.size());
  std::vector<int> triIndices;
  triIndices.reserve(kDefaultTriIndexReserve);

  for (uint32_t faceIdx = 0; faceIdx < elem->count; ++faceIdx) {
    std::vector<int> faceIndices;
    faceIndices.reserve(span_at(counts, faceIdx));
    for (uint32_t vertexIndex = 0; vertexIndex < span_at(counts, faceIdx); ++vertexIndex) {
      int idx = 0;
      copy_and_convert_to(&idx, std::as_bytes(std::span{ data }.subspan(faceOffset, srcValBytes)), prop.type);
      faceIndices.push_back(idx);
      faceOffset += srcValBytes;
    }
    triIndices.resize(static_cast<size_t>(span_at(counts, faceIdx) - 2U) * kIndicesPerTriangle);
    triangulate_polygon(PolygonVertexCount{ span_at(counts, faceIdx) },
      positions,
      meshVertexCount,
      PolygonIndices{ faceIndices },
      TriangleDestination{ triIndices });
    for (int const idx : triIndices) {
      std::array<std::byte, kScalarValueBytes> idxBytes{};
      write_value(std::span{ idxBytes }.subspan(0, sizeof(int)), idx);
      copy_and_convert(destBytes.subspan(destByteOffset, destValBytes),
        destType,
        std::span<const std::byte>{ idxBytes }.subspan(0, sizeof(int)),
        PLYPropertyType::Int);
      destByteOffset += destValBytes;
    }
  }
  return true;
}


bool PLYReader::extract_triangles_convert_src(uint32_t propIdx,
  std::span<const float> positions,
  MeshVertexCount meshVertexCount,
  PLYPropertyType destType,
  void *dest) const
{
  PLYElement const *elem = element();
  PLYProperty const &prop = elem->properties.at(propIdx);
  std::span<const uint32_t> const counts = prop.rowCount;
  std::span<const uint8_t> const data = prop.listData;
  size_t destByteOffset = 0;
  size_t const srcValBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const destValBytes = kPLYPropertySize.at(static_cast<size_t>(destType));
  size_t faceOffset = 0;
  auto destBytes = byte_span(dest, data.size());
  std::span<int> const destInts(static_cast<int *>(dest), destBytes.size() / sizeof(int));

  for (uint32_t faceIdx = 0; faceIdx < elem->count; ++faceIdx) {
    std::vector<int> faceIndices;
    faceIndices.reserve(span_at(counts, faceIdx));
    for (uint32_t vertexIndex = 0; vertexIndex < span_at(counts, faceIdx); ++vertexIndex) {
      int idx = 0;
      copy_and_convert_to(&idx, std::as_bytes(std::span{ data }.subspan(faceOffset, srcValBytes)), prop.type);
      faceIndices.push_back(idx);
      faceOffset += srcValBytes;
    }
    size_t const triCapacity = static_cast<size_t>(span_at(counts, faceIdx) - 2U) * kIndicesPerTriangle;
    uint32_t const numTris = triangulate_polygon(PolygonVertexCount{ span_at(counts, faceIdx) },
      positions,
      meshVertexCount,
      PolygonIndices{ faceIndices },
      TriangleDestination{ destInts.subspan(destByteOffset / sizeof(int), triCapacity) });
    destByteOffset += static_cast<size_t>(numTris) * kIndicesPerTriangle * destValBytes;
  }
  return true;
}


bool PLYReader::extract_triangles_convert_dst(uint32_t propIdx,
  std::span<const float> positions,
  MeshVertexCount meshVertexCount,
  PLYPropertyType destType,
  void *dest) const
{
  PLYElement const *elem = element();
  PLYProperty const &prop = elem->properties.at(propIdx);
  std::span<const uint32_t> const counts = prop.rowCount;
  std::span<const uint8_t> const data = prop.listData;
  size_t destByteOffset = 0;
  size_t const srcValBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const destValBytes = kPLYPropertySize.at(static_cast<size_t>(destType));
  size_t faceOffset = 0;
  auto destBytes = byte_span(dest, data.size());
  std::vector<int> triIndices;
  triIndices.reserve(kDefaultTriIndexReserve);

  for (uint32_t faceIdx = 0; faceIdx < elem->count; ++faceIdx) {
    std::vector<int> faceIndices;
    faceIndices.reserve(span_at(counts, faceIdx));
    for (uint32_t vertexIndex = 0; vertexIndex < span_at(counts, faceIdx); ++vertexIndex) {
      int const idx = read_value<int32_t>(std::as_bytes(std::span{ data }.subspan(faceOffset, srcValBytes)));
      faceIndices.push_back(idx);
      faceOffset += srcValBytes;
    }
    triIndices.resize(static_cast<size_t>(span_at(counts, faceIdx) - 2U) * kIndicesPerTriangle);
    triangulate_polygon(PolygonVertexCount{ span_at(counts, faceIdx) },
      positions,
      meshVertexCount,
      PolygonIndices{ faceIndices },
      TriangleDestination{ triIndices });
    for (int const idx : triIndices) {
      std::array<std::byte, kScalarValueBytes> idxBytes{};
      write_value(std::span{ idxBytes }.subspan(0, sizeof(int)), idx);
      copy_and_convert(destBytes.subspan(destByteOffset, destValBytes),
        destType,
        std::span<const std::byte>{ idxBytes }.subspan(0, sizeof(int)),
        PLYPropertyType::Int);
      destByteOffset += destValBytes;
    }
  }
  return true;
}


bool PLYReader::extract_triangles_native(uint32_t propIdx,
  std::span<const float> positions,
  MeshVertexCount meshVertexCount,
  PLYPropertyType destType,
  void *dest) const
{
  PLYElement const *elem = element();
  PLYProperty const &prop = elem->properties.at(propIdx);
  std::span<const uint32_t> const counts = prop.rowCount;
  std::span<const uint8_t> const data = prop.listData;
  size_t destByteOffset = 0;
  size_t const srcValBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const destValBytes = kPLYPropertySize.at(static_cast<size_t>(destType));
  size_t faceOffset = 0;
  auto destBytes = byte_span(dest, data.size());
  std::span<int> const destInts(static_cast<int *>(dest), destBytes.size() / sizeof(int));

  for (uint32_t faceIdx = 0; faceIdx < elem->count; ++faceIdx) {
    std::vector<int> faceIndices;
    faceIndices.reserve(span_at(counts, faceIdx));
    for (uint32_t vertexIndex = 0; vertexIndex < span_at(counts, faceIdx); ++vertexIndex) {
      faceIndices.push_back(read_value<int32_t>(std::as_bytes(std::span{ data }.subspan(faceOffset, srcValBytes))));
      faceOffset += srcValBytes;
    }
    size_t const triCapacity = static_cast<size_t>(span_at(counts, faceIdx) - 2U) * kIndicesPerTriangle;
    uint32_t const numTris = triangulate_polygon(PolygonVertexCount{ span_at(counts, faceIdx) },
      positions,
      meshVertexCount,
      PolygonIndices{ faceIndices },
      TriangleDestination{ destInts.subspan(destByteOffset / sizeof(int), triCapacity) });
    destByteOffset += static_cast<size_t>(numTris) * kIndicesPerTriangle * destValBytes;
  }
  return true;
}


bool PLYReader::find_pos(std::span<uint32_t, 3> propIdxs) const
{
  static constexpr std::array<const char *, 3> names{ "x", "y", "z" };
  return find_properties(propIdxs, names);
}


bool PLYReader::find_normal(std::span<uint32_t, 3> propIdxs) const
{
  static constexpr std::array<const char *, 3> names{ "nx", "ny", "nz" };
  return find_properties(propIdxs, names);
}


bool PLYReader::find_texcoord(std::span<uint32_t, 2> propIdxs) const
{
  static constexpr std::array<const char *, 2> uvNames{ "u", "v" };
  static constexpr std::array<const char *, 2> stNames{ "s", "t" };
  static constexpr std::array<const char *, 2> textureUvNames{ "texture_u", "texture_v" };
  static constexpr std::array<const char *, 2> textureStNames{ "texture_s", "texture_t" };
  return find_properties(propIdxs, uvNames) || find_properties(propIdxs, stNames)
         || find_properties(propIdxs, textureUvNames) || find_properties(propIdxs, textureStNames);
}


bool PLYReader::find_color(std::span<uint32_t, 3> propIdxs) const
{
  static constexpr std::array<const char *, 3> shortNames{ "r", "g", "b" };
  static constexpr std::array<const char *, 3> longNames{ "red", "green", "blue" };
  return find_properties(propIdxs, shortNames) || find_properties(propIdxs, longNames);
}


bool PLYReader::find_indices(std::span<uint32_t, 1> propIdxs) const
{
  static constexpr std::array<const char *, 1> pluralNames{ "vertex_indices" };
  static constexpr std::array<const char *, 1> singularNames{ "vertex_index" };
  return find_properties(propIdxs, pluralNames) || find_properties(propIdxs, singularNames);
}


bool PLYReader::refill_buffer()
{
  if (m_file == nullptr || m_atEOF) { return false; }

  if (m_pos == 0U && m_end == m_bufDataEnd && m_bufDataEnd == static_cast<size_t>(kPLYReadBufferSize)) { return false; }

  auto const bufSize = static_cast<int64_t>(m_bufDataEnd);
  if (std::cmp_less(bufSize, static_cast<int64_t>(kPLYReadBufferSize))) {
    m_buf.at(static_cast<size_t>(bufSize)) = m_buf.at(static_cast<size_t>(kPLYReadBufferSize));
    m_buf.at(static_cast<size_t>(kPLYReadBufferSize)) = '\0';
    m_bufDataEnd = static_cast<size_t>(kPLYReadBufferSize);
  }

  size_t const keep = m_bufDataEnd - m_pos;
  if (keep > 0U && m_pos > 0U) {
    std::memmove(m_buf.data(), std::span{ m_buf }.subspan(m_pos, keep).data(), keep);
    m_bufOffset += static_cast<int64_t>(m_pos);
  }
  if (m_end >= m_pos) {
    m_end -= m_pos;
  } else {
    m_end = 0U;
  }
  m_pos = 0U;

  size_t const readSize = static_cast<size_t>(kPLYReadBufferSize) - keep;
  size_t const readCount =
    fread(std::span{ m_buf }.subspan(keep, readSize).data(), sizeof(char), readSize, m_file.get());
  if (readCount < readSize && ferror(m_file.get()) != 0) { return false; }

  size_t const fetched = readCount + keep;
  m_atEOF = fetched < static_cast<size_t>(kPLYReadBufferSize);
  m_bufDataEnd = fetched;

  if (!m_inDataSection || m_fileType == PLYFileType::ASCII) { return rewind_to_safe_char(); }
  return true;
}


bool PLYReader::rewind_to_safe_char()
{
  if (!m_atEOF && m_bufDataEnd > 0U
      && (m_buf.at(m_bufDataEnd - 1U) == '\n' || !is_safe_buffer_end(m_buf.at(m_bufDataEnd - 1U)))) {
    size_t safe = m_bufDataEnd - 2U;
    while (safe >= m_end && (m_buf.at(safe) == '\n' || !is_safe_buffer_end(m_buf.at(safe)))) {
      if (safe == 0U) { break; }
      --safe;
    }
    if (safe < m_end) { return false; }
    ++safe;
    m_buf.at(static_cast<size_t>(kPLYReadBufferSize)) = m_buf.at(safe);
    m_bufDataEnd = safe;
  }
  m_buf.at(m_bufDataEnd) = '\0';
  return true;
}


bool PLYReader::accept()
{
  m_pos = m_end;
  return true;
}


bool PLYReader::advance()
{
  m_pos = m_end;
  while (true) {
    while (m_pos < m_bufDataEnd && is_whitespace(char_at(m_pos))) { ++m_pos; }
    if (m_pos == m_bufDataEnd) {
      m_end = m_pos;
      if (refill_buffer()) { continue; }
      return false;
    }
    break;
  }
  m_end = m_pos;
  return true;
}


bool PLYReader::next_line()
{
  m_pos = m_end;
  while (true) {
    while (m_pos < m_bufDataEnd && char_at(m_pos) != '\n') { ++m_pos; }
    if (m_pos == m_bufDataEnd) {
      m_end = m_pos;
      if (refill_buffer()) { continue; }
      return false;
    }
    ++m_pos;
    m_end = m_pos;
    if (!match("comment") && !match("obj_info")) { return true; }
  }
}


bool PLYReader::match(std::string_view str)
{
  m_end = m_pos;
  size_t strIndex = 0U;
  while (m_end < m_bufDataEnd && strIndex < str.size() && char_at(m_end) == str.at(strIndex)) {
    ++m_end;
    ++strIndex;
  }
  return strIndex == str.size();
}


bool PLYReader::which(std::span<const std::string_view> values, uint32_t *index)
{
  for (uint32_t valueIndex = 0; valueIndex < static_cast<uint32_t>(values.size()); ++valueIndex) {
    if (keyword(span_at(values, valueIndex))) {
      *index = valueIndex;
      return true;
    }
  }
  return false;
}


bool PLYReader::which_property_type(PLYPropertyType *type)
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


bool PLYReader::keyword(std::string_view keywordText)
{ return match(keywordText) && (m_end >= m_bufDataEnd || !is_keyword_part(char_at(m_end))); }


bool PLYReader::identifier(std::span<char> dest)
{
  m_end = m_pos;
  if (dest.empty() || m_end >= m_bufDataEnd || !is_keyword_start(char_at(m_end))) { return false; }
  while (m_end < m_bufDataEnd && is_keyword_part(char_at(m_end))) { ++m_end; }

  size_t const len = m_end - m_pos;
  if (len >= dest.size()) { return false; }
  std::memcpy(dest.data(), std::span{ m_buf }.subspan(m_pos, len).data(), len);
  span_ref(dest, len) = '\0';
  return true;
}


bool PLYReader::int_literal(int *value)
{
  size_t endPos = m_pos;
  bool const success = miniply::int_literal(std::span{ m_buf }.subspan(0, m_bufDataEnd), m_pos, endPos, value);
  if (success) { m_end = endPos; }
  return success;
}


bool PLYReader::float_literal(float *value)
{
  size_t endPos = m_pos;
  bool const success = miniply::float_literal(std::span{ m_buf }.subspan(0, m_bufDataEnd), m_pos, endPos, value);
  if (success) { m_end = endPos; }
  return success;
}


bool PLYReader::double_literal(double *value)
{
  size_t endPos = m_pos;
  bool const success = miniply::double_literal(std::span{ m_buf }.subspan(0, m_bufDataEnd), m_pos, endPos, value);
  if (success) { m_end = endPos; }
  return success;
}


bool PLYReader::parse_elements()
{
  m_elements.reserve(kElementReserve);
  while (m_valid && keyword("element")) { parse_element(); }
  return true;
}


bool PLYReader::parse_element()
{
  int count = 0;
  auto tmpSpan = std::span{ m_tmpBuf }.subspan(0, static_cast<size_t>(kPLYTempBufferSize));

  m_valid = keyword("element") && advance() && identifier(tmpSpan) && advance() && int_literal(&count) && next_line();
  if (!m_valid || count < 0) { return false; }

  m_elements.emplace_back();
  PLYElement &elem = m_elements.back();
  elem.name = m_tmpBuf.data();
  elem.count = static_cast<uint32_t>(count);
  elem.properties.reserve(kPropertyReserve);

  while (m_valid && keyword("property")) { parse_property(elem.properties); }
  return true;
}


bool PLYReader::parse_property(std::vector<PLYProperty> &properties)
{
  PLYPropertyType type = PLYPropertyType::None;
  PLYPropertyType countType = PLYPropertyType::None;

  m_valid = keyword("property") && advance();
  if (!m_valid) { return false; }

  if (keyword("list")) {
    m_valid = advance() && which_property_type(&countType) && advance();
    if (!m_valid) { return false; }
  }

  auto tmpSpan = std::span{ m_tmpBuf }.subspan(0, static_cast<size_t>(kPLYTempBufferSize));
  m_valid = which_property_type(&type) && advance() && identifier(tmpSpan) && next_line();
  if (!m_valid) { return false; }

  properties.emplace_back();
  PLYProperty &prop = properties.back();
  prop.name = m_tmpBuf.data();
  prop.type = type;
  prop.countType = countType;
  return true;
}


bool PLYReader::load_fixed_size_element(PLYElement &elem)
{
  size_t const numBytes = static_cast<size_t>(elem.count) * static_cast<size_t>(elem.rowStride);
  m_elementData.resize(numBytes);

  if (m_fileType == PLYFileType::ASCII) {
    if (!load_fixed_ascii_element(elem)) { return false; }
  } else if (!load_fixed_binary_element(elem, numBytes)) {
    return false;
  }

  m_elementLoaded = true;
  return true;
}


bool PLYReader::load_fixed_ascii_element(PLYElement &elem)
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (!load_ascii_scalar_property(prop, back)) {
        m_valid = false;
        return false;
      }
    }
    next_line();
  }
  return true;
}


bool PLYReader::load_fixed_binary_element(PLYElement const &elem, size_t numBytes)
{
  size_t dstOffset = 0U;
  while (dstOffset < numBytes) {
    size_t bytesAvailable = m_bufDataEnd - m_pos;
    if (dstOffset + bytesAvailable > numBytes) { bytesAvailable = numBytes - dstOffset; }
    std::memcpy(std::span{ m_elementData }.subspan(dstOffset, bytesAvailable).data(),
      std::span{ m_buf }.subspan(m_pos, bytesAvailable).data(),
      bytesAvailable);
    m_pos += bytesAvailable;
    m_end = m_pos;
    dstOffset += bytesAvailable;
    if (!refill_buffer()) { break; }
  }
  if (dstOffset < numBytes) {
    m_valid = false;
    return false;
  }

  if (m_fileType == PLYFileType::BinaryBigEndian) { endian_swap_loaded_fixed_element(elem); }
  return true;
}


void PLYReader::endian_swap_loaded_fixed_element(PLYElement const &elem)
{
  size_t dataOffset = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty const &prop : elem.properties) {
      size_t const propBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
      endian_swap(std::as_writable_bytes(std::span{ m_elementData }.subspan(dataOffset, propBytes)), prop.type);
      dataOffset += propBytes;
    }
  }
}


bool PLYReader::load_variable_size_element(PLYElement &elem)
{
  m_elementData.resize(static_cast<size_t>(elem.count) * static_cast<size_t>(elem.rowStride));

  for (PLYProperty &prop : elem.properties) {
    if (prop.countType != PLYPropertyType::None) {
      prop.listData.reserve(static_cast<size_t>(elem.count)
                            * static_cast<size_t>(kPLYPropertySize.at(static_cast<size_t>(prop.type))) * 3U);
    }
  }

  if (m_fileType == PLYFileType::Binary) {
    if (!load_variable_binary_element(elem)) { return false; }
  } else if (m_fileType == PLYFileType::ASCII) {
    if (!load_variable_ascii_element(elem)) { return false; }
  } else if (!load_variable_binary_big_endian_element(elem)) {
    return false;
  }

  m_elementLoaded = true;
  return true;
}


bool PLYReader::load_variable_binary_element(PLYElement &elem)
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (prop.countType == PLYPropertyType::None) {
        m_valid = load_binary_scalar_property(prop, back);
      } else {
        load_binary_list_property(prop);
      }
    }
  }
  return m_valid;
}


bool PLYReader::load_variable_ascii_element(PLYElement &elem)
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (prop.countType == PLYPropertyType::None) {
        m_valid = load_ascii_scalar_property(prop, back);
      } else {
        load_ascii_list_property(prop);
      }
    }
    next_line();
  }
  return m_valid;
}


bool PLYReader::load_variable_binary_big_endian_element(PLYElement &elem)
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (prop.countType == PLYPropertyType::None) {
        m_valid = load_binary_scalar_property_big_endian(prop, back);
      } else {
        load_binary_list_property_big_endian(prop);
      }
    }
  }
  return m_valid;
}


bool PLYReader::load_ascii_scalar_property(PLYProperty &prop, size_t &destIndex)
{
  std::array<uint8_t, kScalarValueBytes> value{};
  if (!ascii_value(prop.type, value)) { return false; }

  size_t const numBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  std::memcpy(std::span{ m_elementData }.subspan(destIndex, numBytes).data(), value.data(), numBytes);
  destIndex += numBytes;
  return true;
}


bool PLYReader::load_ascii_list_property(PLYProperty &prop)
{
  int count = 0;
  m_valid = (prop.countType < PLYPropertyType::Float) && int_literal(&count) && advance() && (count >= 0);
  if (!m_valid) { return false; }

  size_t const numBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const back = prop.listData.size();
  prop.rowCount.push_back(static_cast<uint32_t>(count));
  prop.listData.resize(back + (numBytes * static_cast<size_t>(count)));

  for (int itemIndex = 0; itemIndex < count; ++itemIndex) {
    if (!ascii_value(prop.type,
          std::span{ prop.listData }.subspan(back + (static_cast<size_t>(itemIndex) * numBytes), kScalarValueBytes))) {
      m_valid = false;
      return false;
    }
  }
  return true;
}


bool PLYReader::load_binary_scalar_property(PLYProperty &prop, size_t &destIndex)
{
  size_t const numBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  if (!ensure_bytes_available(numBytes)) { return false; }
  std::memcpy(std::span{ m_elementData }.subspan(destIndex, numBytes).data(),
    std::span{ m_buf }.subspan(m_pos, numBytes).data(),
    numBytes);
  m_pos += numBytes;
  m_end = m_pos;
  destIndex += numBytes;
  return true;
}


bool PLYReader::load_binary_list_property(PLYProperty &prop)
{
  size_t const countBytes = kPLYPropertySize.at(static_cast<size_t>(prop.countType));
  if (!ensure_bytes_available(countBytes)) { return false; }

  int count = 0;
  copy_and_convert_to(&count, std::as_bytes(std::span{ m_buf }.subspan(m_pos, countBytes)), prop.countType);
  if (count < 0) {
    m_valid = false;
    return false;
  }

  m_pos += countBytes;
  m_end = m_pos;

  size_t const listBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type)) * static_cast<size_t>(count);
  if (!ensure_bytes_available(listBytes)) { return false; }

  size_t const back = prop.listData.size();
  prop.rowCount.push_back(static_cast<uint32_t>(count));
  prop.listData.resize(back + listBytes);
  std::memcpy(std::span{ prop.listData }.subspan(back, listBytes).data(),
    std::span{ m_buf }.subspan(m_pos, listBytes).data(),
    listBytes);

  m_pos += listBytes;
  m_end = m_pos;
  return true;
}


bool PLYReader::load_binary_scalar_property_big_endian(PLYProperty &prop, size_t &destIndex)
{
  size_t const startIndex = destIndex;
  if (load_binary_scalar_property(prop, destIndex)) {
    endian_swap(std::as_writable_bytes(
                  std::span{ m_elementData }.subspan(startIndex, kPLYPropertySize.at(static_cast<size_t>(prop.type)))),
      prop.type);
    return true;
  }
  return false;
}


bool PLYReader::load_binary_list_property_big_endian(PLYProperty &prop)
{
  size_t const countBytes = kPLYPropertySize.at(static_cast<size_t>(prop.countType));
  if (!ensure_bytes_available(countBytes)) { return false; }

  int count = 0;
  std::array<std::byte, kScalarValueBytes> tmp{};
  std::memcpy(tmp.data(), std::span{ m_buf }.subspan(m_pos, countBytes).data(), countBytes);
  endian_swap(std::span{ tmp }.subspan(0, countBytes), prop.countType);
  copy_and_convert_to(&count, std::span<const std::byte>{ tmp }.subspan(0, countBytes), prop.countType);
  if (count < 0) {
    m_valid = false;
    return false;
  }

  m_pos += countBytes;
  m_end = m_pos;

  size_t const typeBytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const listBytes = typeBytes * static_cast<size_t>(count);
  if (!ensure_bytes_available(listBytes)) { return false; }

  size_t const back = prop.listData.size();
  prop.rowCount.push_back(static_cast<uint32_t>(count));
  prop.listData.resize(back + listBytes);

  std::memcpy(std::span{ prop.listData }.subspan(back, listBytes).data(),
    std::span{ m_buf }.subspan(m_pos, listBytes).data(),
    listBytes);
  endian_swap_array(std::as_writable_bytes(std::span{ prop.listData }.subspan(back, listBytes)), prop.type, count);

  m_pos += listBytes;
  m_end = m_pos;
  return true;
}


bool PLYReader::ascii_value(PLYPropertyType propType, std::span<uint8_t> value)
{
  auto valueBytes = std::as_writable_bytes(value.subspan(0, kScalarValueBytes));
  int tmpInt = 0;

  switch (propType) {
  case PLYPropertyType::Char:
  case PLYPropertyType::UChar:
  case PLYPropertyType::Short:
  case PLYPropertyType::UShort:
    m_valid = int_literal(&tmpInt);
    break;
  case PLYPropertyType::Int:
  case PLYPropertyType::UInt: {
    int parsed = 0;
    m_valid = int_literal(&parsed);
    if (m_valid) { write_value(valueBytes.subspan(0, kInt32Bytes), static_cast<uint32_t>(parsed)); }
    break;
  }
  case PLYPropertyType::Float: {
    float parsed = 0.0F;
    m_valid = float_literal(&parsed);
    if (m_valid) { write_value(valueBytes.subspan(0, kFloatBytes), parsed); }
    break;
  }
  case PLYPropertyType::Double:
  default: {
    double parsed = 0.0;
    m_valid = double_literal(&parsed);
    if (m_valid) { write_value(valueBytes.subspan(0, kDoubleBytes), parsed); }
    break;
  }
  }

  if (!m_valid) { return false; }
  advance();

  switch (propType) {
  case PLYPropertyType::Char:
    write_value(valueBytes.subspan(0, 1), static_cast<int8_t>(tmpInt));
    break;
  case PLYPropertyType::UChar:
    valueBytes.front() = std::byte{ static_cast<uint8_t>(tmpInt) };
    break;
  case PLYPropertyType::Short:
    write_value(valueBytes.subspan(0, 2), static_cast<int16_t>(tmpInt));
    break;
  case PLYPropertyType::UShort:
    write_value(valueBytes.subspan(0, 2), static_cast<uint16_t>(tmpInt));
    break;
  default:
    break;
  }
  return true;
}


uint32_t triangulate_polygon(PolygonVertexCount vertexCount,
  std::span<const float> positions,
  MeshVertexCount meshVertexCount,
  PolygonIndices indices,
  TriangleDestination destination)
{
  auto const count = static_cast<uint32_t>(vertexCount);
  if (count < kVerticesPerTriangle) { return 0U; }

  if (count == kVerticesPerTriangle) {
    span_ref(destination.values, 0) = span_at(indices.values, 0);
    span_ref(destination.values, 1) = span_at(indices.values, 1);
    span_ref(destination.values, 2) = span_at(indices.values, 2);
    return 1U;
  }

  if (count == kQuadrilateralVertices) {
    span_ref(destination.values, 0) = span_at(indices.values, 0);
    span_ref(destination.values, 1) = span_at(indices.values, 1);
    span_ref(destination.values, 2) = span_at(indices.values, 3);
    span_ref(destination.values, 3) = span_at(indices.values, 2);
    span_ref(destination.values, 4) = span_at(indices.values, 3);
    span_ref(destination.values, kQuadrilateralIndices - 1U) = span_at(indices.values, 1);
    return 2U;
  }

  auto const meshVerts = static_cast<uint32_t>(meshVertexCount);
  for (uint32_t index = 0; index < count; ++index) {
    if (span_at(indices.values, index) < 0 || std::cmp_greater_equal(span_at(indices.values, index), meshVerts)) {
      return 0U;
    }
  }

  Vec3 const origin = vertex_at(positions, span_at(indices.values, 0));
  Vec3 const faceU = normalize(vertex_at(positions, span_at(indices.values, 1)) - origin);
  Vec3 const faceNormal =
    normalize(cross(faceU, normalize(vertex_at(positions, span_at(indices.values, count - 1U)) - origin)));
  Vec3 const faceV = normalize(cross(faceNormal, faceU));

  std::vector<Vec2> points2D(count, Vec2{ .x = 0.0F, .y = 0.0F });
  for (uint32_t index = 1; index < count; ++index) {
    Vec3 const point = vertex_at(positions, span_at(indices.values, index)) - origin;
    points2D.at(index) = Vec2{ .x = dot(point, faceU), .y = dot(point, faceV) };
  }

  std::vector<uint32_t> next(count, 0U);
  std::vector<uint32_t> prev(count, 0U);
  std::span<uint32_t> const nextSpan{ next };
  std::span<uint32_t> const prevSpan{ prev };
  RingLinks const ring{ .prev = prevSpan, .next = nextSpan };
  uint32_t first = 0U;
  for (uint32_t index = 0, prevIndex = count - 1U; index < count; ++index) {
    next.at(prevIndex) = index;
    prev.at(index) = prevIndex;
    prevIndex = index;
  }

  uint32_t remaining = count;
  size_t dstIdx = 0U;
  while (remaining > kVerticesPerTriangle) {
    uint32_t bestIndex = first;
    float bestAngle = angle_at_vert(first, points2D, ring);
    for (uint32_t index = span_at(nextSpan, first); index != first; index = span_at(nextSpan, index)) {
      float const angle = angle_at_vert(index, points2D, ring);
      if (angle < bestAngle) {
        bestIndex = index;
        bestAngle = angle;
      }
    }

    uint32_t const nextIndex = span_at(nextSpan, bestIndex);
    uint32_t const prevIndex = span_at(prevSpan, bestIndex);

    span_ref(destination.values, dstIdx++) = span_at(indices.values, bestIndex);
    span_ref(destination.values, dstIdx++) = span_at(indices.values, nextIndex);
    span_ref(destination.values, dstIdx++) = span_at(indices.values, prevIndex);

    if (bestIndex == first) { first = nextIndex; }
    next.at(prevIndex) = nextIndex;
    prev.at(nextIndex) = prevIndex;
    --remaining;
  }

  span_ref(destination.values, dstIdx++) = span_at(indices.values, first);
  span_ref(destination.values, dstIdx++) = span_at(indices.values, span_at(nextSpan, first));
  span_ref(destination.values, dstIdx++) = span_at(indices.values, span_at(prevSpan, first));

  return count - 2U;
}

}// namespace miniply
