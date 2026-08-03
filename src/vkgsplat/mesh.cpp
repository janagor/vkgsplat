#include "mesh.hpp"

#include <array>
#include <cstddef>
#include <random>

#include "app_state.hpp"
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

namespace {

  inline constexpr f32 kMinColorComponent = 0.05F;
  inline constexpr f32 kMaxColorComponent = 1.0F;

  [[nodiscard]] auto RandomColor(std::mt19937 &rng) -> std::array<f32, 3>
  {
    std::uniform_real_distribution<f32> dist(kMinColorComponent, kMaxColorComponent);
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
  static constexpr std::array<std::array<f32, 2>, 6> kDefaultPositions = { {
    { 0.75F, 0.75F },
    { -0.75F, 0.75F },
    { -0.75F, -0.75F },
    { 0.75F, 0.75F },
    { -0.75F, -0.75F },
    { 0.75F, -0.75F },
  } };
  static constexpr std::array<std::array<f32, 3>, 6> kDefaultColors = { {
    { 0.0F, 0.0F, 0.0F },
    { 1.0F, 0.0F, 0.0F },
    { 0.0F, 1.0F, 0.0F },
    { 0.0F, 0.0F, 0.0F },
    { 0.0F, 1.0F, 0.0F },
    { 0.0F, 0.0F, 1.0F },
  } };

  Mesh mesh{};
  for (size_t i = 0; i < kDefaultPositions.size(); ++i) {
    mesh.add_vertex(kDefaultPositions.at(i), kDefaultColors.at(i));
  }
  return mesh;
}

Mesh Mesh::make_triangle_grid()
{
  Mesh mesh{};
  std::random_device random_device;
  std::mt19937 rng{ random_device() };

  f32 const cell_w = 2.0F / static_cast<f32>(kGridCols);
  f32 const cell_h = 2.0F / static_cast<f32>(kGridRows);

  for (u32 row = 0; row < kGridRows; ++row) {
    for (u32 col = 0; col < kGridCols; ++col) {
      f32 const x0 = -1.0F + (static_cast<f32>(col) * cell_w);
      f32 const y0 = 1.0F - (static_cast<f32>(row + 1) * cell_h);
      auto const color = RandomColor(rng);

      mesh.add_vertex({ x0, y0 }, color);
      mesh.add_vertex({ x0 + cell_w, y0 }, color);
      mesh.add_vertex({ x0, y0 + cell_h }, color);
    }
  }

  return mesh;
}

}// namespace vkgsplat
