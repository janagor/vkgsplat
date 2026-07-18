#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "types.hpp"

namespace vkgsplat {

struct Mesh
{
  std::vector<std::array<f32, 2>> positions;
  std::vector<std::array<f32, 3>> colors;

  void add_vertex(std::array<f32, 2> position, std::array<f32, 3> color);
  void remove_vertex(size_t index);

  [[nodiscard]] size_t vertex_count() const;
  [[nodiscard]] u32 draw_vertex_count() const;

  [[nodiscard]] static Mesh make_default_triangle();

  [[nodiscard]] static Mesh make_triangle_grid();
};

}// namespace vkgsplat
