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

#ifndef MINIPLY_H
#define MINIPLY_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>


/// miniply - A simple and fast parser for PLY files
/// ================================================
///
/// For details about the PLY format see:
/// * http://paulbourke.net/dataformats/ply/
/// * https://en.wikipedia.org/wiki/PLY_(file_format)

namespace miniply {

//
// Constants
//

constexpr uint32_t kInvalidIndex = 0xFFFFFFFFU;

// Standard PLY element names
constexpr std::string_view kPLYVertexElement = "vertex";
constexpr std::string_view kPLYFaceElement = "face";


//
// PLY Parsing types
//

enum class PLYFileType {
  ASCII,
  Binary,
  BinaryBigEndian,
};


enum class PLYPropertyType {
  Char,
  UChar,
  Short,
  UShort,
  Int,
  UInt,
  Float,
  Double,

  None,//!< Special value used in Element::listCountType to indicate a non-list property.
};


struct PLYProperty
{
  std::string name;
  PLYPropertyType type = PLYPropertyType::None;//!< Type of the data. Must be set to a value other than None.
  PLYPropertyType count_type =
    PLYPropertyType::None;//!< None indicates this is not a list type, otherwise it's the type for the list count.
  uint32_t offset = 0;//!< Byte offset from the start of the row.
  uint32_t stride = 0;

  std::vector<uint8_t> list_data;
  std::vector<uint32_t> row_count;// Entry `i` is the number of items (*not* the number of bytes) in row `i`.
};


struct PLYElement
{
  std::string name;//!< Name of this element.
  std::vector<PLYProperty> properties;
  uint32_t count =
    0;//!< The number of items in this element (e.g. the number of vertices if this is the vertex element).
  bool fixed_size = true;//!< `true` if there are only fixed-size properties in this element, i.e. no list properties.
  uint32_t row_stride = 0;//!< The number of bytes from the start of one row to the start of the next, for this element.

  void calculate_offsets();

  /// Returns the index for the named property in this element, or `kInvalidIndex`
  /// if it can't be found.
  uint32_t find_property(const char *prop_name) const;

  /// Return the indices for several properties in one go. Use it like this:
  /// ```
  /// std::array<uint32_t, 3> indexes{};
  /// static constexpr std::array<const char *, 3> names{ "foo", "bar", "baz" };
  /// if (elem.find_properties(indexes, names)) { ... }
  /// ```
  ///
  /// The return value will be true if all properties were found. If it was
  /// not true, you should not use any values from prop_idxs.
  bool find_properties(std::span<uint32_t> prop_idxs, std::span<const char *const> prop_names) const;

  /// Call this on the element at some point before you load its data, when
  /// you know that every row's list will have the same length. It will
  /// replace the single variable-size property with a set of new fixed-size
  /// properties: one for the list count, followed by one for each of the
  /// list values. This will allow miniply to load and extract the property
  /// data a lot more efficiently, giving a big performance increase.
  ///
  /// After you've called this, you must use PLYReader's `extract_columns`
  /// method to get the data, rather than `extract_list_column`.
  ///
  /// The `newPropIdxs` span must have at least `listSize` entries. If the
  /// function returns true, this will have been populated with the indices of
  /// the new properties that represent the list values (i.e. not including the
  /// list count property, which will have the same index as the old list
  /// property).
  ///
  /// The function returns false if the property index is invalid, or the
  /// property it refers to is not a list property. In these cases it will
  /// not modify anything. Otherwise it will return true.
  enum class ListPropertyIndex : uint32_t {};
  enum class FixedListSize : uint32_t {};

  bool
    convert_list_to_fixed_size(ListPropertyIndex list_prop_idx, FixedListSize list_size, std::span<uint32_t> new_prop_idxs);
};


enum class PolygonVertexCount : uint32_t {};
enum class MeshVertexCount : uint32_t {};

struct PolygonIndices
{
  std::span<const int> values;
};

struct TriangleDestination
{
  std::span<int> values;
};


class PLYReader
{
public:
  explicit PLYReader(const char *filename);
  ~PLYReader();

  PLYReader(PLYReader const &) = delete;
  PLYReader &operator=(PLYReader const &) = delete;
  PLYReader(PLYReader &&) = delete;
  PLYReader &operator=(PLYReader &&) = delete;

  [[nodiscard]] bool valid() const;
  [[nodiscard]] bool has_element() const;
  [[nodiscard]] const PLYElement *element() const;
  bool load_element();
  void next_element();

  [[nodiscard]] PLYFileType file_type() const;
  [[nodiscard]] int version_major() const;
  [[nodiscard]] int version_minor() const;
  [[nodiscard]] uint32_t num_elements() const;
  uint32_t find_element(const char *name) const;
  PLYElement *get_element(uint32_t idx);

  /// Check whether the current element has the given name.
  bool element_is(const char *name) const;

  /// Number of rows in the current element.
  [[nodiscard]] uint32_t num_rows() const;

  /// Returns the index for the named property in the current element, or
  /// `kInvalidIndex` if it can't be found.
  uint32_t find_property(const char *name) const;

  /// Equivalent to calling `find_properties` on the current element.
  bool find_properties(std::span<uint32_t> prop_idxs, std::span<const char *const> prop_names) const;

  /// Copy the data for the specified properties into `dest`, which must be
  /// an array with at least enough space to hold all of the extracted column
  /// data.
  ///
  /// `dest_type` specifies the data type for values stored in `dest`. All
  /// property values will be converted to this type if necessary.
  ///
  /// Note that this function does not handle list-valued properties. Use
  /// `extract_list_column()` for those instead.
  bool extract_properties(std::span<const uint32_t> prop_idxs, PLYPropertyType dest_type, void *dest) const;

  /// The same as `extract_properties`, but does not require rows in the
  /// destination to be contiguous: `dest_stride` is the number of bytes
  /// between the start of one row and the start of the next row in the
  /// destination memory.
  bool extract_properties_with_stride(std::span<const uint32_t> prop_idxs,
    PLYPropertyType dest_type,
    void *dest,
    uint32_t dest_stride) const;

  /// Get the array of item counts for a list property. Entry `i` in this
  /// array is the number of items in the `i`th list.
  [[nodiscard]] const uint32_t *get_list_counts(uint32_t prop_idx) const;

  /// Get the sum of all item counts for a list property.
  [[nodiscard]] uint32_t sum_of_list_counts(uint32_t prop_idx) const;

  [[nodiscard]] const uint8_t *get_list_data(uint32_t prop_idx) const;
  bool extract_list_property(uint32_t prop_idx, PLYPropertyType dest_type, void *dest) const;

  [[nodiscard]] uint32_t num_triangles(uint32_t prop_idx) const;
  [[nodiscard]] bool requires_triangulation(uint32_t prop_idx) const;
  bool extract_triangles(uint32_t prop_idx,
    std::span<const float> positions,
    MeshVertexCount mesh_vertex_count,
    PLYPropertyType dest_type,
    void *dest) const;

  bool find_pos(std::span<uint32_t, 3> prop_idxs) const;
  bool find_normal(std::span<uint32_t, 3> prop_idxs) const;
  bool find_texcoord(std::span<uint32_t, 2> prop_idxs) const;
  bool find_color(std::span<uint32_t, 3> prop_idxs) const;
  bool find_indices(std::span<uint32_t, 1> prop_idxs) const;

private:
  using FileHandle = std::unique_ptr<FILE, decltype(&fclose)>;

  bool refill_buffer();
  bool rewind_to_safe_char();
  bool accept();
  bool advance();
  bool next_line();
  bool match(std::string_view str);
  bool which(std::span<const std::string_view> values, uint32_t *index);
  bool which_property_type(PLYPropertyType *type);
  bool keyword(std::string_view keyword_text);
  bool identifier(std::span<char> dest);

  template<class T> bool typed_which(std::span<const std::string_view> values, T *index)
  {
    uint32_t idx = 0;
    if (!which(values, &idx)) { return false; }
    *index = static_cast<T>(idx);
    return true;
  }

  bool int_literal(int *value);
  bool float_literal(float *value);
  bool double_literal(double *value);

  bool parse_elements();
  bool parse_element();
  bool parse_property(std::vector<PLYProperty> &properties);

  bool load_fixed_size_element(PLYElement &elem);
  bool load_variable_size_element(PLYElement &elem);

  bool load_ascii_scalar_property(PLYProperty &prop, size_t &dest_index);
  bool load_ascii_list_property(PLYProperty &prop);
  bool load_binary_scalar_property(PLYProperty &prop, size_t &dest_index);
  bool load_binary_list_property(PLYProperty &prop);
  bool load_binary_scalar_property_big_endian(PLYProperty &prop, size_t &dest_index);
  bool load_binary_list_property_big_endian(PLYProperty &prop);

  bool ascii_value(PLYPropertyType prop_type, std::span<uint8_t> value);

  bool load_fixed_ascii_element(PLYElement &elem);
  bool load_fixed_binary_element(PLYElement const &elem, size_t num_bytes);
  void endian_swap_loaded_fixed_element(PLYElement const &elem);
  bool load_variable_binary_element(PLYElement &elem);
  bool load_variable_ascii_element(PLYElement &elem);
  bool load_variable_binary_big_endian_element(PLYElement &elem);

  bool extract_triangles_convert_both(uint32_t prop_idx,
    std::span<const float> positions,
    MeshVertexCount mesh_vertex_count,
    PLYPropertyType dest_type,
    void *dest) const;
  bool extract_triangles_convert_src(uint32_t prop_idx,
    std::span<const float> positions,
    MeshVertexCount mesh_vertex_count,
    PLYPropertyType dest_type,
    void *dest) const;
  bool extract_triangles_convert_dst(uint32_t prop_idx,
    std::span<const float> positions,
    MeshVertexCount mesh_vertex_count,
    PLYPropertyType dest_type,
    void *dest) const;
  bool extract_triangles_native(uint32_t prop_idx,
    std::span<const float> positions,
    MeshVertexCount mesh_vertex_count,
    PLYPropertyType dest_type,
    void *dest) const;

  void clear_list_property_storage(PLYElement &elem);
  void skip_unloaded_ascii_element(PLYElement const &elem);
  void skip_unloaded_binary_fixed_element(PLYElement const &elem);
  void skip_unloaded_binary_variable_element(PLYElement const &elem);
  void skip_unloaded_binary_big_endian_variable_element(PLYElement const &elem);

  bool ensure_bytes_available(size_t num_bytes);
  char char_at(size_t index) const;

  FileHandle m_file{ nullptr, &fclose };
  std::vector<char> m_buf;
  std::vector<char> m_tmp_buf;
  size_t m_pos = 0;
  size_t m_end = 0;
  size_t m_buf_data_end = 0;
  bool m_in_data_section = false;
  bool m_at_Eof = false;
  int64_t m_buf_offset = 0;

  bool m_valid = false;

  PLYFileType m_file_type = PLYFileType::ASCII;
  int m_major_version = 0;
  int m_minor_version = 0;
  std::vector<PLYElement> m_elements;

  size_t m_current_element = 0;
  bool m_element_loaded = false;
  std::vector<uint8_t> m_element_data;
};


/// Given a polygon with `vertex_count` vertices, where `vertex_count` > 3,
/// triangulate it and store the indices for the resulting triangles in `dst`.
///
/// The triangulation will always produce `vertex_count - 2` triangles, so `dst`
/// must have enough space for `3 * (vertex_count - 2)` indices.
///
/// If `vertex_count == 3`, we simply copy the input indices to `dst`. If
/// `vertex_count < 3`, nothing gets written to dst.
///
/// The return value is the number of triangles.
uint32_t triangulate_polygon(PolygonVertexCount vertex_count,
  std::span<const float> positions,
  MeshVertexCount mesh_vertex_count,
  PolygonIndices indices,
  TriangleDestination destination);

}// namespace miniply

#endif// MINIPLY_H
