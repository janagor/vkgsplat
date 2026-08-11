#ifndef VKGSPLAT_LFD_CONFIG_HPP
#define VKGSPLAT_LFD_CONFIG_HPP

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace vkgsplat {

// LFD quilt defaults (Looking Glass–style parallel camera array).
inline constexpr f64 kDefaultViewConeDegrees = 40.0;
inline constexpr f64 kDefaultLfdFocalDistance = 10.0;

enum class LfdViewLayout : u8 {
  kNormal,
  kFlipRows,
};

struct LfdGridCell
{
  u32 col{};
  u32 row{};
};

[[nodiscard]] auto ParseLfdViewLayout(std::string_view text) -> std::expected<LfdViewLayout, Error>;

[[nodiscard]] auto BuildLfdViewOrder(LfdViewLayout layout, std::array<u32, 2> grid) -> std::vector<u32>;

[[nodiscard]] auto ValidateLfdViewOrder(std::span<u32 const> view_order, std::array<u32, 2> grid)
  -> std::expected<void, Error>;

// Resolve final row-major view order: explicit permutation wins over layout preset.
[[nodiscard]] auto ResolveLfdViewOrder(std::span<u32 const> explicit_order,
  LfdViewLayout layout,
  std::array<u32, 2> grid) -> std::expected<std::vector<u32>, Error>;

// Map an on-screen quilt cell to the logical camera column/row for MakeQuiltView().
[[nodiscard]] auto LfdLogicalCellForGridPosition(std::span<u32 const> view_order,
  std::array<u32, 2> grid,
  u32 grid_col,
  u32 grid_row) -> LfdGridCell;

}// namespace vkgsplat

#endif// VKGSPLAT_LFD_CONFIG_HPP
