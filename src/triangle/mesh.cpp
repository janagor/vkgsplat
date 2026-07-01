#include "mesh.hpp"

#include <array>
#include <cstddef>

#include "types.hpp"

namespace vkgsplat {

void Mesh::add_vertex(std::array<f32, 2> position, std::array<f32, 3> color)
{
  positions.push_back(position);
  colors.push_back(color);
}

void Mesh::remove_vertex(size_t index)
{
  if (index >= positions.size()) { return; }
  auto const offset = static_cast<std::ptrdiff_t>(index);
  positions.erase(positions.begin() + offset);
  colors.erase(colors.begin() + offset);
}

size_t Mesh::vertex_count() const { return positions.size(); }

u32 Mesh::draw_vertex_count() const { return static_cast<u32>(positions.size()); }

Mesh Mesh::make_default_triangle()
{
  static constexpr std::array<std::array<f32, 2>, 6> k_default_positions = { {
    { 0.75F, 0.75F },
    { -0.75F, 0.75F },
    { -0.75F, -0.75F },
    { 0.75F, 0.75F },
    { -0.75F, -0.75F },
    { 0.75F, -0.75F },
  } };
  static constexpr std::array<std::array<f32, 3>, 6> k_default_colors = { {
    { 0.0F, 0.0F, 0.0F },
    { 1.0F, 0.0F, 0.0F },
    { 0.0F, 1.0F, 0.0F },
    { 0.0F, 0.0F, 0.0F },
    { 0.0F, 1.0F, 0.0F },
    { 0.0F, 0.0F, 1.0F },
  } };

  Mesh mesh{};
  for (size_t i = 0; i < k_default_positions.size(); ++i) {
    mesh.add_vertex(k_default_positions.at(i), k_default_colors.at(i));
  }
  return mesh;
}

}// namespace vkgsplat
