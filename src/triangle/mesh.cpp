#include "mesh.hpp"

#include <array>
#include <cstddef>
#include <random>

#include "app_state.hpp"
#include "types.hpp"

namespace vkgsplat {

namespace {

inline constexpr f32 k_min_color_component = 0.05F;
inline constexpr f32 k_max_color_component = 1.0F;

[[nodiscard]] auto random_color(std::mt19937 &rng) -> std::array<f32, 3>
{
  std::uniform_real_distribution<f32> dist(k_min_color_component, k_max_color_component);
  return { dist(rng), dist(rng), dist(rng) };
}

}// namespace

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

Mesh Mesh::make_triangle_grid()
{
  Mesh mesh{};
  std::random_device random_device;
  std::mt19937 rng{ random_device() };

  f32 const cell_w = 2.0F / static_cast<f32>(k_grid_cols);
  f32 const cell_h = 2.0F / static_cast<f32>(k_grid_rows);

  for (u32 row = 0; row < k_grid_rows; ++row) {
    for (u32 col = 0; col < k_grid_cols; ++col) {
      f32 const x0 = -1.0F + (static_cast<f32>(col) * cell_w);
      f32 const y0 = -1.0F + (static_cast<f32>(row) * cell_h);
      auto const color = random_color(rng);

      mesh.add_vertex({ x0, y0 }, color);
      mesh.add_vertex({ x0 + cell_w, y0 }, color);
      mesh.add_vertex({ x0, y0 + cell_h }, color);
    }
  }

  return mesh;
}

}// namespace vkgsplat
