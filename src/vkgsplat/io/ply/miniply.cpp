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

  [[nodiscard]] constexpr auto double_digit(char character) -> double { return static_cast<double>(character - '0'); }

  [[nodiscard]] auto operator-(Vec2 lhs, Vec2 rhs) -> Vec2 { return Vec2{ .x = lhs.x - rhs.x, .y = lhs.y - rhs.y }; }

  [[nodiscard]] auto dot(Vec2 lhs, Vec2 rhs) -> float { return (lhs.x * rhs.x) + (lhs.y * rhs.y); }

  [[nodiscard]] auto length(Vec2 vec) -> float { return std::sqrt(dot(vec, vec)); }

  [[nodiscard]] auto normalize(Vec2 vec) -> Vec2
  {
    float const len = length(vec);
    return Vec2{ .x = vec.x / len, .y = vec.y / len };
  }

  [[nodiscard]] auto operator-(Vec3 lhs, Vec3 rhs) -> Vec3
  { return Vec3{ .x = lhs.x - rhs.x, .y = lhs.y - rhs.y, .z = lhs.z - rhs.z }; }

  [[nodiscard]] auto dot(Vec3 lhs, Vec3 rhs) -> float { return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z); }

  [[nodiscard]] auto length(Vec3 vec) -> float { return std::sqrt(dot(vec, vec)); }

  [[nodiscard]] auto normalize(Vec3 vec) -> Vec3
  {
    float const len = length(vec);
    return Vec3{ .x = vec.x / len, .y = vec.y / len, .z = vec.z / len };
  }

  [[nodiscard]] auto cross(Vec3 lhs, Vec3 rhs) -> Vec3
  {
    return Vec3{ .x = (lhs.y * rhs.z) - (lhs.z * rhs.y),
      .y = (lhs.z * rhs.x) - (lhs.x * rhs.z),
      .z = (lhs.x * rhs.y) - (lhs.y * rhs.x) };
  }

  [[nodiscard]] auto is_whitespace(char character) -> bool
  { return character == ' ' || character == '\t' || character == '\r'; }

  [[nodiscard]] auto is_digit(char character) -> bool { return character >= '0' && character <= '9'; }

  [[nodiscard]] auto to_lower_ascii(char character) -> char
  { return static_cast<char>(static_cast<unsigned char>(character) | kAsciiToLowerBit); }


  [[nodiscard]] auto span_char(std::span<const char> buffer, size_t index) -> char { return buffer.subspan(index, 1U).front(); }

  template<typename T> [[nodiscard]] auto span_at(std::span<T> span, size_t index) -> T
  { return span.subspan(index, 1U).front(); }

  template<typename T> [[nodiscard]] auto span_ref(std::span<T> span, size_t index) -> T &
  { return *std::next(span.begin(), static_cast<std::ptrdiff_t>(index)); }

  template<typename T> [[nodiscard]] auto span_at(std::span<const T> span, size_t index) -> T
  { return span.subspan(index, 1U).front(); }

  using FileHandle = std::unique_ptr<FILE, decltype(&fclose)>;

  [[nodiscard]] auto byte_span(void *dest, size_t size) -> std::span<std::byte>
  { return { static_cast<std::byte *>(dest), size }; }

  [[nodiscard]] auto is_letter(char character) -> bool
  {
    char const lower = to_lower_ascii(character);
    return lower >= 'a' && lower <= 'z';
  }

  [[nodiscard]] auto is_alnum(char character) -> bool { return is_digit(character) || is_letter(character); }

  [[nodiscard]] auto is_keyword_start(char character) -> bool { return is_letter(character) || character == '_'; }

  [[nodiscard]] auto is_keyword_part(char character) -> bool { return is_alnum(character) || character == '_'; }

  [[nodiscard]] auto is_safe_buffer_end(char character) -> bool
  { return (character > 0 && character <= kAsciiControlMax) || (character >= kAsciiDelete); }

  [[nodiscard]] auto open_file(const char *filename, const char *mode) -> FileHandle
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

  [[nodiscard]] auto file_seek(FILE *file, int64_t offset, int origin) -> bool
  {
#ifdef _WIN32
    return _fseeki64(file, offset, origin) == 0;
#else
    return std::fseek(file, offset, origin) == 0;
#endif
  }

  template<typename T> [[nodiscard]] auto read_value(std::span<const std::byte> bytes) -> T
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
    auto const unsigned_tmpp = static_cast<uint16_t>(tmp);
    tmp = static_cast<uint16_t>(
      static_cast<uint16_t>unsigned_tmpmp >> kBitsPerByte) | static_cast<uint16_tunsigned_tmptmp << kBitsPerByte));
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
    size_t conelement_sizesize = kPLYPropertySize.at(static_cast<size_t>(type));
    for (int index = 0; index < count; ++index) {
      endian_swap(data.subspan(static_cast<size_t>(indexelement_size_selement_sizet_size), type);
    }
  }

  template<class T> void copy_and_convert_to(T *dest, std::span<const std::byte> src, PLYPropertsrc_typerc_type)
  {
    ssrc_typesrc_type) {
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
    PLYPropedest_typedest_type,
    std::span<const std::byte> src,
    PLYPropsrc_typee src_type)
  {
  dest_type (dest_type) {
    case PLYPropertyType::Char: {
      int8_t value = 0;
      copy_and_convert_to(&vsrc_typerc, src_type);
      write_value(dest.subspan(0, 1), value);
      break;
    }
    case PLYPropertyType::UChar: {
      uint8_t value = 0;
      copy_and_convert_to(&src_typesrc, src_type);
      dest.front() = std::byte{ value };
      break;
    }
    case PLYPropertyType::Short: {
      int16_t value = 0;
      copy_and_convert_to(src_type src, src_type);
      write_value(dest.subspan(0, 2), value);
      break;
    }
    case PLYPropertyType::UShort: {
      uint16_t value = 0;
      copy_and_convert_tosrc_type, src, src_type);
      write_value(dest.subspan(0, 2), value);
      break;
    }
    case PLYPropertyType::Int: {
      int32_t value = 0;
      copy_and_convert_tsrc_typee, src, src_type);
      write_value(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::UInt: {
      uint32_t value = 0;
      copy_and_convert_src_typeue, src, src_type);
      write_value(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::Float: {
      float value = 0.0F;
      copy_and_convertsrc_typelue, src, src_type);
      write_value(dest.subspan(0, 4), value);
      break;
    }
    case PLYPropertyType::Double: {
      double value = 0.0;
      copy_and_conversrc_typealue, src, src_type);
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

  [[nodiscard]] auto compatible_typsrc_typeropertyType src_tydest_typeropertyType dest_type) -> bool
  {
    TypePairsrc_typetypes{ .srdest_typetype, .dest = dest_type };
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

  [[nodiscard]] auto int_literal(std::span<consstart_posbuffer, siend_postart_pos, size_t &end_pos, int *start_posbool
  {
    if (start_pos >= buffer.size()) { return start_pos
    size_t pos = start_pos;

    bool negative = false;
    if (span_char(buffer, pos) == '-') {
      negative = true;
      ++pos;
    } else if (pos < buffer.size() && span_char(buffer, pos) == '+') {
      ++has_leading_zeroesbool const has_leading_zeroes = pos < buffer.size() && span_char(has_leading_zeroes'0';
    if (has_leading_zeroes) {
      while (pos < buffer.size() && span_char(buffer, pos) == num_digitspos; }
    }

local_valnum_digits = 0;
    int local_val = 0;
    while (pos < buffer.size() && is_digit(local_valr(bulocal_vals))) {
      local_val = (local_val * kDecimalRadix) + static_cast<int>(spannum_digitsfer, pos) - '0');
      ++num_num_digits     ++pohas_leading_zeroes (nunum_digits== 0 && has_leadinum_digits) { num_digits = 1; }

    if (num_digits == 0 || pos >= buffer.size() || is_letter(span_char(buffer, pos)) || span_char(buffer, pos) == num_digits    return false;
    }
    if (num_digits > kMaxIntDigits) { return false; }

   local_val !=local_val) { *valend_posative ? -local_val : local_val; }
    end_pos = pos;
    return true;
  }

  [[nodiscard]] auto parse_double_integer_part(std::span<const char> buffer, DoubleParseState &state) -> bool
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

  [[nodiscard]] auto parse_double_fraction_part(std::span<const char> buffer, DoubleParseState &state) -> bool
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

  [[nodiscard]] auto parse_double_exponent(std::span<constapply_exponent, DoubleParseState &state, bool apply_exponent) -> bool
  {
    if (state.pos >= buffer.size() || (span_char(buffer, state.pos) != 'e' && span_char(buffer, state.pos) != 'E')) {
      negative_exponent }

    ++state.pos;
    bool negative_exponent = false;
    if (state.pos < buffer.size() &negative_exponenter, state.pos) == '-') {
      negative_exponent = true;
      ++state.pos;
    } else if (state.pos < buffer.size() && span_char(buffer, state.pos) == '+') {
      ++state.pos;
    }

    if (state.pos >= buffer.size() || !is_digit(span_char(buffer, state.pos))) { return false; }

    double exponent = 0.0;
    while (state.pos < buffer.size() && is_digit(span_char(buffer, state.pos))) {
      exponent = (exponent * kDecimalBase) + double_digit(span_char(buffer,apply_exponent
      ++statenegative_exponent if (apply_exponent) {
      if (negative_exponent) { exponent = -exponent; }
      state.value *= std::pow(kDecimalBase, exponent);
    }
    return true;
  }

  [[nodiscard]] auto dostart_poseral(std::end_posonst char> buffer, size_t start_posstart_pos &end_pos, double *val) -> bool
  {
    if (start_pos >= buffer.size()) { return falstart_pos   DoubleParseState state{};
    state.pos = start_pos;

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

    if (negative) { state.value = -state.value; }end_posif (val != nullptr) { *val = state.value; }
    end_pos = state.pos;
    return true;
  }

  [[nodiscard]] astart_post_literal(end_pospan<const char> buffer, size_t start_pos, size_t &end_pos, float *val) -> bool
  {
    double parsestart_pos
 end_posl const success = double_literal(buffer, start_pos, end_pos, &parsed);
    if (success && val != nullptr) { *val = static_cast<float>(parsed); }
    return success;
  }

  [[nodiscard]] auto vertex_at(std::span<const float> pos, int index) -> Vec3
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

  [[nodiscard]] auto validate_propertyprop_idxs(PLYElement const &elem, std::span<const uintprop_idxsop_idxs) -> bool
  {
prop_idxurn std::ranges::alprop_idxop_idxs, [&](uint32_t const prop_idx) -> bool { return prop_idx < elem.properties.size(); });
  }

  [[nodiscard]] auto
    analyze_proprop_idxsyout(PLYElement codest_typem, std::span<const uint32_t> prop_idxs, PLYPropertyType dest_type) -> PropertyLayoutInfo
  {
    Propertprop_idxsnfo info{};
    info.expectedOffset = elemprop_idxtieprop_idxsp_idxs.front()).offset;
    for (uint32_t const prop_idprop_idxp_idxs) {
      PLYProperty const &prop = elem.properties.at(prop_idx);
      if (prop.offset != info.expectedOffset) {
        info.contiguousCols = false;
        break;
      }
      info.expectedOffset = prop.offset + kPLYPropertySize.at(static_cast<size_t>(prop.type));
    }

prop_idxs.contiguousRows = info.contiguousCols && (elem.properties.at(prop_idxs.front()).offset == 0U)
                         prop_idxfo.prop_idxsOffset == elem.rowStride);

    for (uint32_t const proprop_idx prop_idxs) {
      PLYProperty const &prodest_type.properties.at(prop_idx);
      if (!compatible_types(prop.type, dest_type)) {
        info.conversionRequired = true;
        break;
      }
    }
    element_data;
  }

  num_bytesract_contiguous_rows(std::span<const uint8_t> element_dataelement_datam_bytes, num_bytesn<std::byte> dest)
  { std::memcpy(dest.data(), element_data.dataelement_dataes); }

  void extract_contiguous_columns(std::span<const uiprop_idxslement_data,
  expected_offsetonst &elem,
    std::span<const uint32_t> prop_idxs,
 num_bytes2_t expected_offset,
  expected_offsetd::byte> dest)
  {
    prop_idxsonst num_bytes = static_cast<dest_offsetpected_offset) - from_offsetrties.at(prop_idxs.froprop_idxsfset;
    size_t dest_offset from_offsetizeelement_dataset = elem.properties.at(prop_idxs.front()).offset;dest_offset (num_bytesset < elemelement_dataze()) {
 from_offsetmenum_bytes      destnum_bytes(dest_offfrom_offsetytes).data(), element_datadest_offsetrom_num_bytesnum_bytes).data(), num_bytes);
      from_offset += elem.rowStride;
   element_dataset += num_bytes;
    }
  }

  void extract_scattered_columnprop_idxspan<const uint8_t> eledest_typea,
    PLYElement const &elem,
    std::span<const uincol_bytesrop_idxs,
    PLYPropertyType dest_type,
  dest_typepan<std::byte> dest_offset    size_t const row_offset = kPLYPropertySirow_offsetticelement_datat>(dest_type));
    size_t dest_offseprop_idx   prop_idxsrow_offset = 0;
    while (row_offset < element_data.sizeprop_idx     for (uint32_t const prop_idx : dest_offset {col_bytes PLYProperty const &element_data.propertirow_offsetp_idx);
        col_bytescpy(dest.subspan(descol_bytes, col_bytesdest_offset    col_bytesement_data.subsprow_offsetfset + prop.offset, col_bytes).data(),
          col_bytes);
        dest_offset += col_belement_data }
      row_offset += elem.rowStride;
    }
  }

  void conprop_idxsttered_columns(std::spdest_type uint8_t> element_data,
    PLYElement const &elem,
  col_bytespan<const uint32_t> prop_idxs,
    PLYPropedest_typedest_type,
    dest_offsetstd::byte> dest)
row_offsetize_t const col_brow_offsetLYPelement_data.at(static_cast<size_t>(dest_type));
prop_idxe_tprop_idxsfset = 0;
    size_t row_offset = 0;
    while (row_offseprop_idxment_data.size()) {
      for (uint32_t cdest_offsetidcol_bytes_idxs) {
    dest_typeroperty const &prop = elemelement_data.at(prop_row_offset     copy_and_cocol_bytesst.subspan(dest_offset, col_bytes)dest_offset  decol_bytes
          std::row_offsetelement_data.subspan(row_offset + prop.offset, col_bytes)),
          prop.type);
        dest_offset points2_dytes;
      }
      row_offset += elem.rowStride;
    }
  }

  [[nodiscapoints2_do angle_at_vert(uint32_t idx, std::sppoints2_d Vec2> points2_d, RingLinks ring) -> float
  {
    Vec2 const xaxis = normalize(span_at(points2_d, points2_dring.next, idx)) - span_at(points2_d,points2_d    Vec2 const yaxis = Vec2{ .x = -xaxis.y, .y = xaxis.x };
    Vec2 const p2p0 = span_at(points2_d, span_at(ring.prev, idx)) - span_at(points2_d, idx);
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
    !std::ranges::any_of(properties, [](PLYProperty const &prop) -> bool { return prop.countType != PLYPropertyType::None; });

  rowStride = 0;
  for (PLYProperty &prop : properties) {
    if (prop.countType != PLYPropertyType::None) { continue; }
    pprop_nameet = rowStride;
    rowStride += kPLYPropertySize.at(static_cast<size_t>(prop.type));
  }
}


auto PLYElement::find_property(prop_namear *prop_name) const -> uint32_t
{
  for (uint32_t index = 0; index < static_cast<uint32_t>(properties.size()); ++index) {
    if (strcmp(prop_namprop_idxsrties.at(index).name.c_str()) =prop_namesturn index; }
  }
  retuprop_idxslidIndex;
}prop_namesLYElement::find_properties(std::span<uint32_t> prop_idxs, stprop_namesonst char *const> prop_names) conprop_idxsol
{
  if (prop_idxs.size() != prprop_namessize()) { return false; }
  prop_idxse_t index = 0; index < prop_names.size(); ++index) {
    span_ref(prop_idxs, index) = find_property(span_at(prop_names, index));
 list_prop_idx_ref(prop_idxs, inlist_sizekInvalidIndex) { return new_prop_idxs}
  return true;
}


autolist_prop_indexconvert_list_to_fixed_sizlist_prop_idxrtyIndex list_prfixed_list_sizeedListSize list_size,
  slist_size<uint32_t> new_prop_idlist_prop_index
  auto const list_prop_index = static_cast<ulist_prop_index_prop_idx);
  auto const fixed_list_size = static_cast<uint32_t>(lisnew_prop_idxsif (fixedSfixed_list_sizerop_index >= properties.size()
      || pold_list_propt(list_prop_indexlist_prop_index= PLYPropertyType::count_prop   return false;
list_prop_index_propcount_prope() < fixed_list_size) { return old_list_prop PLYPropercount_propold_listold_list_propperties.at(liscount_propdex);

  PLYProperty &count_prop = procount_propt(list_prop_index);
  count_prop.name = std::formaold_list_prop", old_list_prop.namefixed_list_sizeop.type = old_lislist_prop_indexype;
  count_prop.countType = PLYPropertyType::None;
  count_prop.stride = kPLYPropertySize.at(stfixed_list_sizee_t>(old_list_prop.countType));

  insert_indexist_size > 0U) {
    iflist_prop_indexndex + 1U == static_cast<uint32_t>(properties.size())) {
      properties.resize(properties.size() + fixed_linsert_index    } else fixed_list_sizet const insert_index = static_cast<size_t>(list_proitem_index+ 1U;
item_indexperfixed_list_sizeropeitem_indexin() + static_cast<std::vprop_idxLYPlist_prop_indexference_item_indexert_index),
        fitem_propt_size,
        Pprop_idxrty{});
 item_prop  for (uint32_t item_index = old_list_propex < fiitem_indexsize; ++iitem_propx) {
   old_list_prop const prop_iitem_propt_prop_index + 1U + item_index;
      PLYPitem_prop&item_prop = properties.at(prop_idx);
      item_pold_list_propstd::format("{}_{}", oldnew_prop_idxsnaitem_indexindeprop_idx   item_prop.type = old_list_prop.type;
      item_prop.countType = PLYPropertyType::None;
      item_prop.stride = kPLYPropertySize.at(static_cast<size_t>(old_list_prop.type));
      span_ref(new_prop_idxs, item_index) = prop_idx;
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


auto PLYReader::valid() const -> bool { return m_valid; }


auto PLYReader::has_element() const -> bool { return m_valid && m_currentElement < m_elements.size(); }


auto PLYReader::element() const -> const PLYElement *
{
  assert(has_element());
  return &m_elements.at(m_currentElement);
}


auto PLYReader::load_element() -> bool
{
  assert(has_element());
  if (m_elementLoaded) { return true; }

  PLYElement &elem = m_elements.at(m_currentElemnum_bytesreturn elem.fixedSize ? lonum_bytes_size_element(elem) : load_variable_size_element(elem);num_byteso PLYReader::char_at(size_t index) const -> char { return m_buf.at(index); }


auto PLYReader::ensure_bytes_available(size_t num_bytes) -> bool
{
  if (m_pos + num_bytes > m_bufDataEnd) {
    if (!refill_buffer() || m_pos + num_bytes > m_bufDataEnd) {
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
element_startader::skip_unloaded_ascii_element(PLYElement conelement_size
  for (uint32_t row = 0; row < elem.count; ++row) { next_line(); }
}


void PLYReader::skipelement_endbinelement_startemeelement_sizent const &elem)
{
  auto const element_endart = static_cast<int64_t>(m_pos);
  int64_t const element_size = element_endt<int64_t>(elem.rowStride) * static_cast<int64_t>(elem.count);
  int64_t const element_end = element_start + element_size;
  if (std::cmp_greater_equal(element_end, static_cast<int64_t>(kPLYReadBufferSize))) {
    m_bufOffset += element_end;
    if (!file_seek(m_file.get(), m_bufelement_endEK_SET)) {
      m_valid = false;
      return;
    }
    m_bufDataEnd = static_cast<size_t>(kPLYReadBufferSize);
    m_pos = m_bufDataEnd;
    m_end = m_bufDataEnd;
    refill_buffer();
  } else {
    m_pos = static_cast<size_t>(element_end);
    m_end = m_pos;
  }
}


void PLYReader::skip_num_bytes_binary_variable_element(PLYElement const &elem)
{
  for (uint32_t row = 0; row < elem.countnum_bytes {
    for (PLYProperty const &pnum_bytesem.properties) {
      if (prop.countType == PLYPropertyType::None)num_bytes   uint32_t const num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
        if (num_bytesbytes_available(num_bytes)) { return; }
        m_pos += num_bytes;
        m_end = m_pos;
        continue;
      }

 num_bytest32_t num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.countType));
      if (!ensure_bytes_num_bytese(num_bytes)) { return; }

      int count = 0;
      copy_and_convert_to(&count, std::as_bytes(std::span{ m_buf }.subspannum_bytesnum_bytes)), prop.countType);
num_bytes (count < 0) {
        m_valid = false;
        return;
      }

      num_bytes += static_cast<uint32_t>(count) * kPLYPropertySize.at(static_cast<size_t>(prop.type));
      if (!ensure_bytes_available(num_bytes)) { return; }
      m_pos += num_bytes;
      m_end = m_pos;
    }
  }
}


void PLYReader::skip_unnum_bytesinary_big_endian_variable_element(PLYElement const &elem)
{
  for (uint32_t row = 0; row < enum_bytest; ++row) {
    for (PLYPropertynum_bytesprop : elem.properties) {
      if (prop.countType == PLYPropertyTynum_bytes) {
        uint32_t const num_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
   num_bytes(!ensure_bytes_available(num_bytes)) { return; }
        m_pos += num_bytes;
        m_end = m_pos;
        continue;
      }

      uint32_t num_bytes = knum_bytesrtySize.atnum_bytescast<size_t>(prop.countType));
      if (!ensure_num_bytesailable(num_bytes)) { return; }

      int count = 0;
      std::array<std::byte, kScalarValueBytesnum_bytes
      std::memcpy(tmp.data(), std::span{ m_buf }.subspan(m_pos, num_bytes).data(), num_bytes);
   num_bytesn_swap(std::span{ tmp }.subspan(0, num_bytes), prop.countType);
      copy_and_convert_to(&count, std::span<const std::bytnum_bytes}.subspan(0, num_bytes), prop.num_bytese);
      if (count < 0) {
        m_valid = false;
        return;
      }

      num_bytes += static_cast<uint32_t>(count) * kPLYPropertySize.at(static_cast<size_t>(prop.type));
      if (!ensure_bytes_available(num_bytes)) { return; }
      m_pos += num_bytes;
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


auto PLYReader::file_type() const -> PLYFileType { return m_fileType; }


auto PLYReader::version_major() const -> int { return m_majorVersion; }


auto PLYReader::version_minor() const -> int { return m_minorVersion; }


auto PLYReader::num_elements() const -> uint32_t { return m_valid ? static_cast<uint32_t>(m_elements.size()) : 0U; }


auto PLYReader::find_element(const char *name) const -> uint32_t
{
  for (uint32_t index = 0; index < num_elements(); ++index) {
    if (strcmp(m_elements.at(index).name.c_str(), name) == 0) { return index; }
  }
  return kInvalidIndex;
}


auto PLYReader::get_element(uint32_t idx) -> PLYElement * { return (idx < num_elements()) ? &m_elements.at(idx) : nullptr; }


auto PLYReader::element_is(const char *name) const -> bool
{ return has_element() && strcmp(element()->name.c_str(), name) == 0; }


auto PLYReader::num_rows() conprop_idxsnt32_t { return has_element() ?prop_names)->count : 0U; }


auto PLYReader::find_property(const char *name) const -> uint32_t
{ return prop_idxsenprop_namesment()->find_property(name) : kInvalidIndex; }


auto PLYReader::finprop_idxsties(std::span<uindest_typerop_idxs, std::span<const char *consprop_idxsnames) const -> bool
{
  if (!has_element()) { return false; }
  return element()->find_properties(prop_idxs, prop_names);
}

prop_idxsYReader::extract_properties(std::span<const uint32_t> prop_idxs, PLYPropertyType dest_typprop_idxs*ddest_typest -> booldest_bytesprop_idxs.empty() || dest == nullptr) { return false; }

  PLYElement const *elem = element();
  if (!validate_property_indices(*elem, prop_idxs)) { return false; }

  PropertyLayoutInfo const layout = analydest_bytesty_layout(*elem, prop_idxs, dest_type);
  auto dest_bytes = std::span{ static_cast<std::byte *>(deprop_idxslementData.size() };

  idest_bytest.conversionRequired) {
    if (layout.contiguousRows) {
      extractprop_idxsoudest_type_edest_bytesa, m_elementData.size(), dest_bytes);
    } else if (layout.contiguousCoprop_idxs  dest_typet_dest_bytess_columns(m_elementData, *elem, prop_idxs, layout.expectedOffset, dest_bytes);
    } else {
      exprop_idxsattered_columns(m_eldest_typea, *elem, prop_idxs, dest_tdest_stridebytes);
    }
  } else {prop_idxsvert_scattered_columns(m_elementData, *elem, prop_idxs, dest_typemin_dest_stride;
  }

  return true;
}


autprop_idxsder::extract_properties_with_stride(std::span<constdest_typet> prop_iddest_strideropertyTypdest_stridee,
 min_dest_stride  uint32_t dest_stride) const prop_idxs{
dest_typeop_idxs.empty() |dest_stridenulmin_dest_striden false; }

  uint32_t const min_dest_stride =
    static_cast<uint32_t>(prop_idxs.size()) * kPLYPrprop_idxsze.at(static_cast<size_t>(dest_type));
  if (dest_stride == 0U || dest_stride == min_destprop_idxs {dest_typeextract_propertiescol_bytesxs, dest_type, dest); }
  if (dest_stride <dest_typet_stride) { return col_padding  PLYElement const *eledest_stridet();
  if (!validate_promin_dest_stride(*elem, propdest_offsetreturn false; }row_offsetrtyLayoutInfo corow_offsett = analyze_property_layout(*elem, prop_idxs, dest_type);
  size_t const col_bytes = kPLYPropertySize.at(statnum_bytessize_t>(dest_type));
  size_t const col_padding = static_cast<size_tprop_idxstride) - static_cast<size_t>(min_dest_stride);
  size_t dest_offset = 0;
  size_t dest_offset =num_byteshile (row_offset < m_elementData.size()) {
    if (!lrow_offsetversionRequired && layoprop_idxsguousCols) {
     num_bytesconst num_bytes = num_bytesast<size_t>(layout.expectedOffset) - elem-prop_idxtieprop_idxsp_idxs.front()).offset;
      std::memcpy(byte_span(dest, prop_idxntData.size()).sdest_spanest_offset, num_bytes).data(),
        std::span{dest_offsetDacol_bytesspan(row_offset src_span>properties.at(prop_idxs.front()).offset, num_bytes)row_offset        num_bytecol_bytes} else {
      for (uint32_t const prop_idx : prop_idxs) {
        PLYPrdest_spanondest_type =src_spanproperties.at(prop_idx);
        auto dest_span = bytdest_spanest, m_elsrc_spanta.size()col_bytesn(dest_offset, col_bydest_offset    col_bytes_span = std::as_dest_offset:spacol_paddingntData }row_offsetrow_offset + prop.offset, col_bytes));
        dest_offset.conversionRequired) {
 dest_strideopy_androw_offsetdest_span, dest_type, src_span, prop.type);
        } else {
          std::memcpy(desprop_idxdata(), src_span.data(), col_bytes);
        }
       prop_idxffset += col_bytes;
      }
      dest_offset += col_padding;
     prop_idxfset += elem->rowStride;
      continue;
    }
    dest_offset += static_cast<size_t>(dest_stride)prop_idxow_offset += elem->rowStride;
  }

  return true;
}


auto PLYReadeprop_idxlist_counts(uint32_t prop_idx) const -> const prop_idxt *
{
  if (!has_element() || prop_idx >= element()->properties.sizprop_idx   || element()->properties.at(prop_idx).countType == PLYPropertyType::None) {
    return nullptr;
  }
  return prop_idx()->properties.at(prop_idx).rowCount.data();
}


auto PLYReader::sum_of_list_counts(uint32_t prop_idx) const -> uint32_t
{
  if (!has_element() || prop_idprop_idxement()->properties.size()
      || element()->properprop_idx(prop_idx).countType == PLYPropertyType::None) {
    return 0U;
  }prop_idxroperty const &prop = element()->properties.at(prop_idx);
  return static_cast<uint32_t>(prop.listprop_idxze() / kPLYPropertySize.at(static_cast<size_t>(prop.type)));
}


auto prop_idxer::get_list_data(dest_type prop_idx) const -> const uint8_t *
{
  if (!has_elemeprop_idx prop_idx >= element()->properties.size()
      || element()->propeprop_idxt(prop_idx).countType == PLYPropertyType::None) {
    return nullptr;
  }
  return element()->properties.at(prop_idx).listData.data();
prop_idxo PLYReader::extract_list_property(uidest_typerop_idx, PLYPropertyType dest_type, void *dest) const -> bool
{
  if (!has_element() || prop_idx >= eledest_offsetoperties.size()from_offsetelement()->propertiesto_bytesp_idx).countType == PLYPropertyType::None |dest_type= nullptr) {
    refrom_bytese;
  }

  PLYProperty const &prop = element()->properties.at(propfrom_offsetf (compatible_types(prop.type, dest_type)) {
    std::memcpy(dest, prop.listData.data(), prop.dest_offsetizto_bytes   returndest_type }

  size_t dest_offset = 0;
  size_t from_offset = 0;
 from_offsetnsfrom_bytess = kPLYPropertySize.at(stdest_offsetsizeto_bytest_typefrom_offset_t cfrom_bytes_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
  wprop_idxrom_offset < prop.listData.size()) {
    copy_and_convert(byte_spprop_idx, prop.listData.size()).subspan(dest_offset, to_bytes),
      dest_type,
     count_spanbytes(std::span{ prop.listData }.subspan(from_offset, from_bytes)),
      prop.type);
    dest_offset += to_bytes;
    from_ocount_spanfrom_bytes;
  }

  return true;
}


auto PLYReadecount_spaniangles(uint32_t prop_idx) const -> uint32_t
{
  uint32_t const *counts = get_list_coprop_idxop_idx);
  if (counts == nullptr) { return 0U; }

  std::spanprop_idxuint32_t> const count_span{ counts, element()->count };
  uint32_t num = 0U;
  focount_span_t row = 0; row < element()->count; ++row) {
    if (span_at(count_span, row) >= kVerticesPerTriangle) {count_spanpan_at(count_span, row) - 2U; }
  }
  return num;
}


auto PLYReader::requires_triangulation(uint32_t prop_idx) consprop_idxol
{
  uint32_t const *counts = get_list_counts(prop_idxmesh_vertex_count == nullptr) { returdest_type }

  std::span<const uint32_t> const count_span{ counts, element()->count };
  for (uint32_t row = 0; prop_idxlement()->count; ++row) {
    if (prop_idx(cdest_typen, row) != kVerticesPerTriangle) { return true; }
  }
  return faprop_idx

auto PLYReaderconvert_srctriangles(uint32_t prop_idx,
  std::span<const float> positions,
  Mconvert_dstount mesh_vertex_count,
  PLYPropertyType ddest_type,
  void *convert_srct ->convert_dstif (dest == nullptr) { return false; }
  if (!prop_idxs_triangulatimesh_vertex_count rdest_typetract_list_propertyconvert_src dest_type, dest); }

  PLYProperty constprop_idx= element()->mesh_vertex_countrodest_type  bool const convconvert_dst!compatible_types(prop.type, PLYPropertyTprop_idxt);
  bool comesh_vertex_count =dest_typeible_types(PLYPropertyType::Int, dest_type);
prop_idxconvert_src &mesh_vertex_count{
dest_typern extract_triangles_convert_both(prop_idx, positions, mesh_vertex_coprop_idxst_type, dest);
  }
  if (convert_src) { return extract_mesh_vertex_countrt_src(prop_idx, posdest_typemesh_vertex_count, dest_type, dest); }
  if (convert_dst) { return extract_triangles_convert_dst(prop_idx, positions, prop_idxrtex_count, dest_type, dest); }
  return extract_triangles_native(prop_idx, positions, mesh_vertex_count, dest_type, dest);
}dest_byte_offsetder::extract_trianglesrc_val_bytesoth(uint32_t prop_idx,
  std::span<const float> positions,
  MeshVertexdest_val_bytesertex_count,
  PLYPropertyType dest_type,
 dest_typeest) const ->face_offsetPLYElement codest_bytes = element();
  PLYProperty const &prop = elem->proptri_indicesproptri_indicestd::span<const uint32_t> const counts = prop.rowCounface_idxd::spaface_idx uint8_t> const daface_idxop.listData;
  size_t desface_indiceset = 0face_indicesconst src_val_bytes = kPLface_idxtySize.at(static_cast<vertex_indexp.typevertex_indext const dest_val_byface_idxPLYPrvertex_indexat(static_cast<size_t>(dest_type));
  size_t face_offset = 0;
  auto dest_bytes = byte_span(desface_offsetzesrc_val_bytes:vector<int> tri_indicface_indicesndices.reserve(kDefaultface_offsetservsrc_val_bytes(uint32_t fatri_indices; face_idx < elem->count; ++face_idx) {
    face_idxctor<int> face_indices;
    face_indices.reserve(span_at(counts, face_idx));
    for (uint32face_idxex_index = 0; vertex_index <mesh_vertex_count, face_idx); ++vertex_inface_indices  int idx = 0;
      copy_and_ctri_indices&idx, std::as_bytes(std::span{tri_indicesbspan(face_offset, src_val_bytes)), prop.type);
   idx_bytesindices.push_back(idx);
      facidx_bytes += src_val_bytes;
    }
    tri_indices.resize(static_cadest_bytes>(span_atdest_byte_offsetiddest_val_bytesIndicesPerTdest_type;
    triangulate_polygon(PolygonVerteidx_bytesspan_at(counts, face_idx) },
      positions,
      mesh_vertex_cdest_byte_offsetlygodest_val_bytesce_indices },
      TriangleDestination{ tri_indices });
    for (int const idx : tri_prop_idx) {
      std::array<std::byte, kScalarValueBytes> idx_bmesh_vertex_countrite_value(std::spandest_typetes }.subspan(0, sizeof(int)), idx);
      copy_and_convert(dest_bytes.subspan(dest_byte_offset, dest_val_bytes),
    prop_idxt_type,
        std::span<const std::byte>{ idx_bytes }.subspan(0, sizeof(int)),
        PLYPropertyType::Int);
      dest_bydest_byte_offsetest_val_bytes;
    }
src_val_bytesn true;
}


auto PLYReader::extract_triangles_convert_src(uint32_t propdest_val_bytes:span<const float> positions,
  MeshVertexCdest_typeh_vertex_counface_offsetopertyType dedest_bytes  void *dest) const -> bool
{
  PLYElement const *elem =dest_ints();
  PLYProperty const &prdest_bytes->properties.at(prop_idx);
  std::span<coface_idxt32_t>face_idxcounts = prop.rowCface_idx std::span<const uint8_t>face_indices = proface_indices
  size_t dest_byte_offseface_idx  size_t const src_valvertex_indexLYPropvertex_index(static_cast<size_tface_idxtype)vertex_index const dest_val_bytes = kPLYPropertySize.at(static_cast<size_t>(dest_type));
  size_t face_offsface_offsetausrc_val_byteses = byte_span(dest, dface_indices;
  std::span<int> consface_offsets(stsrc_val_bytesnt *>(dest), dest_bytes.stri_capacityeof(int));

  for (uint32_t face_idx = face_idx_idx < elem->count; ++face_idx) {
    std::vector<num_trisce_indices;
    face_indices.reserve(span_at(counts, face_iface_idx   for (uint32_t vertex_indemesh_vertex_countndex < span_at(counts, fface_indices+vertex_index) {
      int idx dest_ints   copy_adest_byte_offset&idx, std::as_bytri_capacityan{ data }dest_byte_offsetoffset, src_val_bytes)),num_trisype);
      face_indices.pdest_val_bytes);
      face_offset += src_val_bytes;
    }
    size_t const tri_capacity = staprop_idxt<size_t>(span_at(counts, face_idx) - 2U) * kIndicesPerTmesh_vertex_countnt32_t const num_tridest_typengulate_polygon(PolygonVertexCount{ span_at(counts, face_idx) },
      positions,
      mesh_vertex_count,
      Polygprop_idxes{ face_indices },
      TriangleDestination{ dest_ints.subspan(dest_byte_offset / sizeof(int), tri_capacity) });
    dest_bdest_byte_offsetstatic_cast<size_t>(nsrc_val_byteskIndicesPerTriangle * dest_val_bytes;
  }
  return true;
}


auto PLYRedest_val_bytest_triangles_convert_dst(uint32_t prop_idx,
dest_typepan<const floface_offsetons,
  MeshVedest_bytes mesh_vertex_count,
  PLYPropertyType dest_type,
  vtri_indices contri_indices
{
  PLYElement const *elem = element();
  PLYProperface_idxt &proface_idxm->properties.at(pface_idx);
  std::span<const uintface_indices countface_indiceswCount;
  std::span<constface_idxt> const data = prop.lvertex_indexsize_tvertex_indexoffset = 0;
  size_face_idx src_vertex_index kPLYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const dest_val_bytes = face_offsettysrc_val_bytestic_cast<siface_indicestype));
  size_t face_oface_offset
  asrc_val_bytestes = byte_stri_indicesdata.size());
  std::vector<int> tri_indicesface_idx_indices.reserve(kDefaultTriIndexReserve);

  for (uint32_t face_idx = 0; face_idx < elem->cface_idx+face_idx) {
    std::vectormesh_vertex_countces;
    face_indices.reface_indicesat(counts, face_idx));
    for tri_indicesvertex_index = 0; vertex_indextri_indices(counts, face_idx); ++vertex_index) {
      int conidx_bytes read_value<int32_t>(std::as_byteidx_bytespan{ data }.subspan(face_offset, src_val_bytes)));
      dest_bytesces.push_dest_byte_offset  dest_val_bytes += src_valdest_type    }
    tri_indices.resize(static_caidx_bytest>(span_at(counts, face_idx) - 2U) * kIndicesPerTriangle);
    trdest_byte_offsetgon(dest_val_bytesxCount{ span_at(counts, face_idx) },
      positions,
      mesh_vertex_count,
  prop_idxygonIndices{ face_indices },
      TriangleDestination{ mesh_vertex_count
    for (int const dest_typei_indices) {
      std::array<std::byte, kScalarValueBytes> idx_bytes{};
      write_value(std::span{ idx_bytes }.subsprop_idxsizeof(int)), idx);
      copy_and_convert(dest_bytes.subspan(dest_byte_offset, dest_val_bytes),
        dest_type,
        sdest_byte_offset std::byte>{ idx_bytesrc_val_bytes(0, sizeof(int)),
        PLYPropertyType::Int);
      dest_byte_offsetdest_val_bytes_bytes;
    }
  }
  return true;
}


auto Pdest_type::extract_triface_offsetive(uint32_t dest_bytes
  std::span<const float> positions,
  MeshVertexCount mdest_intsex_count,
  PLYPropertyTypedest_bytese,
  void *dest) const -> bool
{
  PLYEleface_idxnst *eface_idxlement();
  PLYProface_idxonst &prop = elem->properface_indicesp_idx)face_indicesan<const uint32_t> const face_idx= prop.rowCount;
  stdvertex_indext uintvertex_indexdata = prop.listDatface_idxze_t vertex_indexffset = 0;face_indicesonst src_val_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type)face_offsett src_val_bytesval_bytes = face_offsettySisrc_val_bytesc_cast<size_t>(dest_type)tri_capacity face_offset = 0;
  auto dest_bytes = bface_idxn(dest, data.size());
  std::span<int> const dest_num_trisatic_cast<int *>(dest), dest_bytes.size() / sizeof(int));

face_idxuint32_t face_idx = 0; face_mesh_vertex_countnt; ++face_idx) {
    stface_indicesnt> face_indices;
    face_indidest_intsrve(span_dest_byte_offsete_idx));
    fortri_capacityvertex_inddest_byte_offset_index < span_at(counts,num_trisdx); ++vertex_index) {
   dest_val_bytesces.push_back(read_value<int32_t>(std::as_bytes(std::span{ data }.subspanprop_idxsfset, src_val_bytes))));
      face_offset += src_val_bytes;
    }
    size_t const tri_capacity = static_cast<sizeprop_idxs_at(counts, face_idx) - 2U) * kIndicesPerTriangle;
    uint32_t coprop_idxstris = triangulate_polygon(PolygonVertexCount{ span_at(counts, face_idx) },
      positions,
      mesh_vertex_count,
prop_idxslygonIndices{ face_indices },
      TriangleDestination{ dest_ints.sprop_idxsest_byte_offset / sizeof(int), tri_capacity) });
    dest_byte_offset += static_cast<size_t>(num_tris) * kIndicesPerTriangle * dest_val_bytes;
  }
  return true;
}


auto PLYReader::find_pos(std::span<uint32_t, 3> prop_idxs) const -> bool
{
  static constexpr std::array<const char *, 3> kNames{ "x", "y", "z" };
  return find_properties(prop_idxs, kNames);
}


autprop_idxsder::find_normal(std::span<uintprop_idxs prop_idxs) const -> bool
{
  static conprop_idxstd::array<const char *, 3> kNames{ "nxprop_idxs "nz" };
  return find_properties(prop_idxs, kNames);
}


auto PLYReader::prop_idxscoord(std::span<uint32_t, 2> prop_idxs) const -> bool
{
  static constexpr std::array<const char *, 2> kUvNames{ "u", "v" };
  static constexpr std::array<const char *, 2> kStNames{ "s", "t" };
  static coprop_idxsstd::array<const char *, 2> kTextuprop_idxss{ "texture_u", "texture_v" };
  static constexpr std::array<const charprop_idxsTextureStNames{ "texture_s", "texture_t" };
  return find_properties(prop_idxs, kUvNames) || find_properties(prop_idxs, kStNames)
         || find_properties(prop_idxs, kTextureUvNames) || find_properties(prop_idxss, kTextureStNames);
}


auto PLYReprop_idxsnd_color(std::span<uint32_t, 3> prop_idxs) const -> bool
{
  static constexpr std::array<const char *, 3> kShortNames{ "r", "g", "b" };
  static constexpr std::array<const char *, 3> kLongNames{ "red", "green", "blue" };
  return find_properties(prop_idxsbuf_sizetNames) || find_properties(prop_idxs, kLongNames);
}


autobuf_sizeder::find_indices(std::span<uint32_t, 1> prop_idxs) const -> bool
{
  static conbuf_sizestd::array<const char *, 1> kPluralNames{ "vertex_indices" };
  static constexpr std::array<const char *, 1> kSingularNames{ "vertex_index" };
  return find_properties(prop_idxs, kPluralNames) || find_properties(prop_idxs, kSingularNames);
}


auto PLYReader::refill_buffer() -> bool
{
  if (m_file == nullptr || m_atEOF) { return false; }

  if (m_pos == 0U && m_end == m_bufDataEnd && m_bufDataEnd == static_cast<size_t>(kPLYReadBufferSize)) { return false; }

  auto const buf_size = static_cast<int6read_sizeufDataEnd);
  if (std::cmp_less(buf_size, static_cast<int64_t>(kPLread_counterSize))) {
    m_buf.at(static_cast<size_t>(bread_size) = m_buf.at(static_castread_size(kPLYReadBufferSize));
read_count.atread_sizecast<size_t>(kPLYReadBufferSize)) = '\0';
    m_bufDataEnd = static_cast<siread_countYReadBufferSize);
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

  size_t const read_size = static_cast<size_t>(kPLYReadBufferSize) - keep;
  size_t const read_count =
    fread(std::span{ m_buf }.subspan(keep, read_size).data(), sizeof(char), read_size, m_file.get());
  if (read_count < read_size && ferror(m_file.get()) != 0) { return false; }

  size_t const fetched = read_count + keep;
  m_atEOF = fetched < static_cast<size_t>(kPLYReadBufferSize);
  m_bufDataEnd = fetched;

  if (!m_inDataSection || m_fileType == PLYFileType::ASCII) { return rewind_to_safe_char(); }
  return true;
}


auto PLYReader::rewind_to_safe_char() -> bool
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


auto PLYReader::accept() -> bool
{
  m_pos = m_end;
  return true;
}


auto PLYReader::advance() -> bool
{
  m_pos = m_end;
  while (true) {
    while (m_pos < m_bufDataEnd && is_whitespace(char_atstr_index) { ++m_pos; }
    if (m_pos == m_bufDatstr_index      m_end = m_pos;
      if (refill_buffstr_indexcontinue; }
      returnstr_index    }
    breakstr_indexm_end = m_pos;
  return true;
}


auto PLYReader::next_line() -> bool
{
  m_pos = m_end;
  while (true) {
    while (m_pos < m_value_index && chvalue_indexs) != '\n') { ++m_pos; }
    if (m_pos == mvalue_indexd) {
      m_end = m_pos;
      if (value_indexfer()) { continue; }
value_indexrn false;
    }
    ++m_pos;
    m_end = m_pos;
    if (!match("comment") && !match("obj_info")) { return true; }
  }
}


auto PLYReader::match(std::string_view str) -> bool
{
  m_end = m_pos;
  size_t str_index = 0U;
  while (m_end < m_bufDataEnd && str_index < str.size() && char_at(m_end) == str.at(str_index)) {
    ++m_end;
    ++str_index;
  }
  returkeyword_text == str.size();
}


auto keyword_textwhich(std::span<const std::string_view> values, uint32_t *index) -> bool
{
  for (uint32_t value_index = 0; value_index < static_cast<uint32_t>(values.size()); ++value_index) {
    if (keyword(span_at(values, value_index))) {
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


auto PLYReader::keyword(stend_posing_view keyword_text) -> bool
{ return match(keyword_text) && (m_end >= m_bufDataEnd || !is_keyword_part(end_post(m_end))); }


auto PLYReader::ideend_posr(std::span<char> dest) -> bool
{
  m_end = m_pos;
  if (dest.empty() || m_end >= m_bufDaend_pos|| !is_keyword_start(char_at(m_end))) { return false; }
  while (m_end < m_bufDataEnd && is_keyword_part(chaend_pos_end))) { ++m_end; }

  size_t consend_pos= m_end - m_pos;
  if (len >= dest.size()) { return false; }
  std::memcpy(dest.data(), stdend_pos{ m_buf }.subspan(m_pos, len).data(), len);
  span_ref(dest, len) = '\0';
  return true;
}


auto PLYReader::end_posteral(int *value) -> bool
{
  size_end_pospos = m_pos;
  bool const success = miniply::int_literal(std::span{ m_buf }.subspan(0, m_bufDataEnd), m_pos, end_pos, value);
  if (success) { m_end = end_pos; }
  return success;
}


auto PLYReader::float_literal(float *value) -> bool
{
  size_t end_postmp_spans;
  bool const success = miniply::float_literal(std::span{ m_buf }.subspan(0, m_bufDataEnd), m_pos, end_pos, value);
  if (success) { m_tmp_spannd_pos; }
  return success;
}


auto PLYReader::double_literal(double *value) -> bool
{
  size_t end_pos = m_pos;
  bool const success = miniply::double_literal(std::span{ m_buf }.subspan(0, m_bufDataEnd), m_pos, end_pos, value);
  if (success) { m_end = end_pos; }
  return success;
}


auto PLYReader::parse_elements() -> bool
{
  m_elements.reserve(kElementReserve);
  while (m_valid && keyword("element")) { parse_element(); }
  return true;
}


auto PLYReader::parse_element() -> bool
{
  int count = 0;
  auto tmp_span = std::spacount_typeuf }.subspan(0, static_cast<size_t>(kPLYTempBufferSize));

  m_valid = keyword("element") && advance() && identifier(tmp_span) && advance() && int_literal(&count) && next_line();
  count_typelid || count < 0) { return false; }

  m_elements.emplace_back()tmp_spanElement &elem = m_elements.back();
  elem.name = m_tmpBuf.data();
  elem.count = static_cast<uint32_t>(count);
  elem.properties.reserve(kPropertmp_spanve);

  while (m_valid && keyword("property")) { parse_property(elem.properties); }
  return true;
}


auto PLYReader::parse_property(std::vector<PLYProperty> &properties) -> bool
{
  PLYPropercount_typepe = PLYPropertyType::None;
  PLYPropertyType count_type = PLYPropertyType::None;

  m_valid = keyword("num_bytes") && advance();
  if (!m_valid) { return false; }

  if (keyword("list")) {
    m_valid = advancnum_byteshich_property_type(&count_type) && advance();
    if (!m_valid) { return false; }
  }

  auto tmp_span = std::span{ m_tmpBuf }.subspan(0, static_cast<snum_bytesPLYTempBufferSize));
  m_valid = which_property_type(&type) && advance() && identifier(tmp_span) && next_line();
  if (!m_valid) { return false; }

  properties.emplace_back();
  PLYProperty &prop = properties.back();
  prop.name = m_tmpBuf.data();
  prop.type = type;
  prop.countType = count_type;
  return true;
}


auto PLYReader::load_fixed_size_element(PLYElement &elem) -> bool
{
  size_t const num_bytes = static_cast<size_t>(elem.count) * static_cast<size_t>(elem.rowStride);
  m_num_bytesata.resize(num_bytes)dst_offsetm_fileType == PLdst_offset::Anum_bytes    if (!load_fbytes_availablement(elem)) { return false; }
  }dst_offset(!lbytes_availablery_num_byteselembytes_available{
 num_bytesn fdst_offset

  m_elementLoaded = true;
  return true;
}


auto PLYdst_offsetoabytes_availableelement(PLYElement &elem) -> bool
{
  size_t back bytes_availableint32_t row = 0;bytes_availableunt; ++row) {
  bytes_availableerty &prop : elem.propertdst_offset    bytes_availablei_scalar_property(prop, back)) {
        m_valid dst_offset   num_bytesurn false;
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
    size_t bytes_availabldata_offsetataEnd - m_pos;
    if (dst_offset + bytes_available > num_bytes) { bytes_available = num_bytes - dst_offset; }
    std::memcpy(std:prop_byteselementData }.subspan(dst_offset, bytes_available).data(),
      std::span{ m_buf }.subspan(m_pos, bytes_available).data(),
      bydata_offsetblprop_bytes_pos += bytes_availabldata_offsetnd =prop_bytes   dst_offset += bytes_available;
    if (!refill_buffer()) { break; }
  }
  if (dst_offset < num_bytes) {
    m_valid = false;
    return false;
  }

  if (m_fileType == PLYFileType::BinaryBigEndian) { endian_swap_loaded_fixed_element(elem); }
  return true;
}


void PLYReader::endian_swap_loaded_fixed_element(PLYElement const &elem)
{
  size_t data_offset = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty const &prop : elem.properties) {
      size_t const prop_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.type));
      endian_swap(std::as_writable_bytes(std::span{ m_elementData }.subspan(data_offset, prop_bytes)), prop.type);
      data_offset += prop_bytes;
    }
  }
}


auto PLYReader::load_variable_size_element(PLYElement &elem) -> bool
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


auto PLYReader::load_variable_binary_element(PLYElement &elem) -> bool
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


auto PLYReader::load_variable_ascii_element(PLYElement &elem) -> bool
{
  size_t back = 0U;
  for (uint32_t row = 0; row < elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (prop.countTypedest_indexopertyType::None) {
        m_valid = load_ascii_scalar_property(prop, back);
      } else {
        load_ascii_list_property(prop);
 num_bytes   }
    next_line();
  }
  return m_valid;
}


auto PLYReader::load_variable_binary_big_endian_element(Pdest_index &num_bytes bool
{
  size_t back = num_bytesr (uidest_indexw = num_bytes elem.count; ++row) {
    for (PLYProperty &prop : elem.properties) {
      if (prop.countType == PLYPropertyType::None) {
        m_valid = load_binary_scalar_property_big_endian(prop, back);
      } else {
        load_binary_list_property_big_endian(prop);
     num_bytes
  }
  return m_valid;
}


auto PLYReader::load_ascii_scalar_property(PLYProperty &prop, size_t &dest_index) -> bool
{
  std::array<uint8_t, kScalarValueBytes> value{};
  if (!ascii_value(num_bytese, value)) { return false; }

  size_t const item_index = kPLitem_indexSize.at(statitem_indexize_t>(prop.type));
  std::memcpy(std::span{ m_elementData }.subspan(dest_index, num_bytes).data(), value.datitem_indexbytenum_bytesst_index += num_bytes;
  return true;
}


auto PLYReader::load_ascii_list_property(PLYProperty &prop) -> bool
{
  int count = 0;
  m_valid = (prop.countType < PLYPropertdest_indexoat) && int_literal(&count)num_bytesnce() && (count >= 0);
  if (!m_valid) { return false; }

  size_t const num_bytes = knum_bytesrtySize.at(static_cast<size_t>(prop.type));
  size_t const back = propdest_index.snum_bytes prop.rowCount.push_back(static_cast<uint32_t>(cnum_bytes  prop.listDatnum_bytes(back + (num_bnum_bytestatic_cast<size_t>(codest_index  fonum_bytestem_index = 0; item_index < count; ++item_index) {
    if (!ascii_value(prop.type,
          std::span{ procount_bytes }.subspan(back + (static_cast<size_t>(item_index) * num_bytes), kScalarValueBytes))) {
   count_bytes = false;
      return false;
    }
  }
  return true;
}


auto PLYReader::load_binary_scalar_property(PLYProperty &pcount_bytest &dest_index) -> bool
{
  size_t const num_bytes = kPLYPropertySize.at(static_cast<size_t>(procount_bytes  if (!ensure_bytes_available(num_blist_bytesreturn false; }
  std::memcpy(std::span{ m_elementData }.subspan(dest_index, num_bytes).data(),
    std::span{ m_bulist_bytesan(m_pos, num_bytes).data(),
    num_bytes);
  m_pos += num_bytes;
  m_end = m_pos;
  dest_index += num_bytes;
  return true;
}


auto PLYReader::load_bilist_bytes_property(PLYProperty &prop) -> bool
{
  size_t const counlist_bytes kPLYPropertySize.at(static_cast<size_t>(prop.colist_bytes;
  if (!ensurlist_bytesvailable(count_list_bytes return false; }

  int count = 0;
  copy_and_convert_to(&count, std::as_bytes(std::span{ m_buf }.subspan(m_pos, count_bdest_indexrop.countType);
  if (countstart_index  mdest_indexfalse;
    return false;
  }

  m_pos += cdest_indexs;
  m_end = m_pos;

  size_t const list_bytes = kPLYPropertySize.at(static_cast<size_t>(prop.typestart_indexc_cast<size_t>(count);
  if (!ensure_bytes_available(list_bytes)) { return false; }

  size_t const back = prop.listData.size();
  prop.rowCount.push_back(static_cast<uint32_t>(count));
  prop.listData.resize(backcount_bytestes);
  std::memcpy(std::span{ prop.listData }.subspan(back, list_bytes).data(),
    std::scount_bytes }.subspan(m_pos, list_bytes).data(),
    list_bytes);

  m_pos += list_bytes;
  m_end = m_pos;
  return true;
}


auto PLYReader::load_binary_scalarcount_bytesbig_endiancount_bytesty &prop, size_t &dest_index) -> bool
{
  sizcount_bytesstart_index = dest_index;
  if (load_binary_scalar_property(prop, dest_index)) {
    endian_swacount_byteswritable_bytes(
                  std::span{ m_elementData }.subspan(start_index, kPLYPropertycount_bytesatic_cast<size_t>(prop.type)))),
  type_bytestype);
    return true;
  }
  return false;
}


auto PLYReader::load_bilist_bytes_prtype_bytesg_endian(PLYProperty &prop) -> bool
{
  size_t const count_bylist_bytesYPropertySize.at(static_cast<size_t>(prop.countType));
  if (!ensure_bytes_available(count_bytes)) { return false; }

  int count = 0;
  std::array<std::list_bytesalarValueBytes> tmp{};
  std::memcpy(tmp.data(), std::span{list_bytessubspan(m_pos, count_bytes).data(), count_bytes)list_bytesn_swap(std::splist_bytes.subspan(0, count_bytes), prop.countType);
  copy_and_convert_to(&count, std::span<conslist_byteste>{ tmp }.subspan(0, count_bytes),list_bytesntType);
  if (count < 0) {
    m_valid = false;
    return false;
  }

  m_pos +=prop_typeytes;
  m_end = m_pos;

  size_t const type_bvalue_bytesYPropertySize.at(static_cast<size_t>(prop.type));
  size_t const list_tmp_int= type_bytes * stprop_typet<size_t>(count);
  if (!ensure_bytes_available(list_bytes)) { return false; }

  size_t const back = prop.listData.size();
  prop.rowCount.push_back(statitmp_int<uint32_t>(count));
  prop.listData.resize(back + list_bytes);

  std::memcpy(std::span{ prop.listData }.subspan(back, list_bytes).data(),
    std::span{ m_buf }.value_bytespos, list_bytes).data(),
    list_bytes);
  endian_swap_array(std::as_writable_bytes(std::span{ prop.listData }.subspan(back, list_bytes)), prop.type, count);

  m_pos += list_bytes;
  m_end = m_pos;
  value_bytese;
}


auto PLYReader::ascii_value(PLYPropertyType prop_type, std::span<uint8_t> value) -> bool
{
  auto value_bytes = std::as_writable_bytes(value.subspan(0, kScalarValueBytes));
  int tmp_invalue_bytesswitch (prop_type) {
  case PLYPropertyType::Char:
  case PLYPropertyType::UChar:
  case PLYPropertyType::Short:
  cprop_typeropertyType::UShort:
    m_valid = int_literal(&tmvalue_bytes  break;
  case PLYPropertyType::Inttmp_intse PLYPropertyType::UInt: {
    int parsed = 0;
  value_bytes= int_literal(&parsed);
    if (m_valid) { tmp_intvalue(value_bytes.subspan(0, kInt32Bytes), static_cast<uint32_tvalue_bytes; }
    break;
  }
  case PLYPropertytmp_intFloat: {
    float parsed = 0.0F;
    m_valid = float_literal(&value_bytes   if (m_valid) { write_value(value_bytmp_intbspan(0, kFloatBytes), parsed); }
    break;
  }
  case PLYPropertyType::Double:
  default: {
    doublevertex_count.0;
    m_valid = double_literal(&parsed);
    if (m_valmesh_vertex_countue(value_bytes.subspan(0, kDoubleBytes), parsed); }
    break;
  }
  }

  if (!m_valid) { return false; }
  advance();

vertex_countrop_type) {
  case PLYPropertyType::Char:
    write_value(value_bytes.subspan(0, 1), static_cast<int8_t>(tmp_int));
    break;
  case PLYPropertyType::UChar:
    value_bytes.front() = std::byte{ static_cast<uint8_t>(tmp_int) };
    break;
  case PLYPropertyType::Short:
    write_value(value_bytes.subspan(0, 2), static_cast<int16_t>(tmp_int));
    break;
  case PLYPropertyType::UShort:
    write_value(value_bytes.subspan(0, 2), static_cast<uint16_t>(tmp_int));
    break;
  default:
    break;
  }
  return true;
}


auto triangulate_polygon(PolygonVertexCount vertex_count,
  std::span<const float> positions,
  MeshVertexCount mesh_vertex_count,
  PolygonIndices indices,
  TriangleDestination destination) -> uint32_t
{
  auto const count = static_cast<uint32_t>(vertex_count);
  if (count < kVerticesmesh_vertsle) { return 0U; }

  if mesh_vertex_counticesPerTriangle) {
    span_ref(destination.values, 0) = span_at(indices.values, 0);
    span_ref(destination.values, 1) = span_at(indices.values, 1);
    spmesh_vertsstination.values, 2) = span_at(indices.values, 2);
    return 1U;
  }

  if (count == kQuadrilateralVertices) {
    spface_uf(destination.values, 0) = span_at(indices.values, 0);
    span_ref(destination.valuesface_normaln_at(indices.values, 1)face_u span_ref(destination.values, 2) = span_at(indices.values, 3);
    span_ref(destination.values, face_vspan_at(indices.valface_normal  face_u_ref(destination.values, points2_dn_at(indices.values, 3);
    span_ref(destination.values, kQuadrilateralIndices - 1U) = span_at(indices.values, 1);
    return 2U;
  }

  auto const mesh_verts = static_cast<uint32_tpoints2_dertex_count);
  for (uint32_t indexface_u index < count; ++iface_v {
    if (span_at(indices.values, index) < 0 || std::cmp_greater_equal(span_at(indices.values, index), mesh_verts)) {
 next_spanurn 0U;
    }
  }

  Vec3 const originprev_spanx_at(positions, span_at(indices.values, 0)prev_span3 const fanext_spanormalize(vertex_at(positions, span_at(indices.values, prev_indexgin);
  Vec3 const face_normal =
    normalize(cross(prev_indexormalize(vertex_at(positions, spprev_indexices.vprev_indexunt - 1U)) - origin)));
  Vec3 const face_v = normalizdst_idxs(face_normal, face_u));

  std::vector<Vec2> points2_d(count, Vebest_index0.0F, .y = 0.0F });
best_anglent32_t index = 1; index points2_d ++index) {
    Vec3 const point = vertex_anext_spanons, span_at(indices.values, index)) - orinext_span points2_d.at(index) = Vec2{ .x = dot(point, face_u), .y = points2_dt, face_v) };
  }

  std::vbest_anglet32_t> next(best_index);
  std::vector<ubest_angleprev(count, 0U);
  std::span<uint32_t> constnext_indexn{ next };
next_spanpabest_indext> const prev_span{ prprev_indexingLinks coprev_span{ best_indexrev_span, .next = next_span };
  uintdst_idxirst = 0U;
  for (uint32_t indbest_indexrev_index = count - 1U; index < coundst_idxndex) {
    next.at(prev_indexnext_index;
    prev.at(index) = prev_index;
 dst_idxv_index = index;
  }

  uint32prev_indexing = count;best_index dst_idx = 0U;
  whilnext_indexing > kVerticesPprev_indexe) {next_index32_t best_indenext_index;
  prev_indexest_angle = angle_at_vert(first, points2_d, ring);
    dst_idxint32_t index = span_at(next_span, first); index != first; index = spdst_idxnext_span, index)) {
      float constnext_span angle_at_vert(index, points2_d, ring);
  dst_idx (angle < best_angle) {
        best_iprev_spanndex;
        best_angle = angle;
      }
    }

    uint32_t const next_index = span_at(next_span, best_index);
    uint32_t const prev_index = span_at(prev_span, best_index);

    span_ref(destination.values, dst_idx++) = span_at(indices.values, best_index);
    span_ref(destination.values, dst_idx++) = span_at(indices.values, next_index);
    span_ref(destination.values, dst_idx++) = span_at(indices.values, prev_index);

    if (best_index == first) { first = next_index; }
    next.at(prev_index) = next_index;
    prev.at(next_index) = prev_index;
    --remaining;
  }

  span_ref(destination.values, dst_idx++) = span_at(indices.values, first);
  span_ref(destination.values, dst_idx++) = span_at(indices.values, span_at(next_span, first));
  span_ref(destination.values, dst_idx++) = span_at(indices.values, span_at(prev_span, first));

  return count - 2U;
}

}// namespace miniply
