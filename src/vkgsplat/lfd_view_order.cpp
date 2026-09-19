#include <vkgsplat/lfd_config.hpp>

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace vkgsplat {

namespace {

  [[nodiscard]] auto GridViewCount(std::array<u32, 2> grid) -> u32 { return grid.at(0) * grid.at(1); }

}// namespace

auto ParseLfdViewLayout(std::string_view text) -> std::expected<LfdViewLayout, Error>
{
  if (text == "normal") { return LfdViewLayout::kNormal; }
  if (text == "flip-rows") { return LfdViewLayout::kFlipRows; }
  return std::unexpected{ MakeError(std::errc::invalid_argument, "lfd-view-layout must be 'normal' or 'flip-rows'") };
}

auto BuildLfdViewOrder(LfdViewLayout layout, std::array<u32, 2> grid) -> std::vector<u32>
{
  u32 const cols = std::max(1U, grid.at(0));
  u32 const rows = std::max(1U, grid.at(1));
  std::vector<u32> order;
  order.reserve(static_cast<size_t>(cols) * static_cast<size_t>(rows));

  for (u32 row = 0U; row < rows; ++row) {
    u32 const logical_row = layout == LfdViewLayout::kFlipRows ? (rows - 1U - row) : row;
    for (u32 col = 0U; col < cols; ++col) { order.push_back((logical_row * cols) + col); }
  }

  return order;
}

auto ValidateLfdViewOrder(std::span<u32 const> view_order, std::array<u32, 2> grid) -> std::expected<void, Error>
{
  u32 const expected_count = GridViewCount(grid);
  if (view_order.size() != static_cast<size_t>(expected_count)) {
    return std::unexpected{ MakeError(
      std::errc::invalid_argument, "lfd-view-order length must equal lfd-grid columns * rows") };
  }

  std::vector<bool> seen(expected_count, false);
  for (u32 const view_index : view_order) {
    if (view_index >= expected_count) {
      return std::unexpected{ MakeError(
        std::errc::invalid_argument, "lfd-view-order entries must be in [0, columns * rows)") };
    }
    if (seen.at(view_index)) {
      return std::unexpected{ MakeError(std::errc::invalid_argument, "lfd-view-order must be a permutation") };
    }
    seen.at(view_index) = true;
  }

  return {};
}

auto ResolveLfdViewOrder(std::span<u32 const> explicit_order, LfdViewLayout layout, std::array<u32, 2> grid)
  -> std::expected<std::vector<u32>, Error>
{
  if (!explicit_order.empty()) {
    auto const validated = ValidateLfdViewOrder(explicit_order, grid);
    if (!validated) { return std::unexpected{ validated.error() }; }
    return std::vector<u32>{ explicit_order.begin(), explicit_order.end() };
  }

  auto built = BuildLfdViewOrder(layout, grid);
  return built;
}

auto LfdLogicalCellForGridPosition(std::span<u32 const> view_order, std::array<u32, 2> grid, u32 grid_col, u32 grid_row)
  -> LfdGridCell
{
  u32 const cols = std::max(1U, grid.at(0));
  u32 const grid_index = (grid_row * cols) + grid_col;
  if (static_cast<size_t>(grid_index) >= view_order.size()) { return {}; }
  u32 const view_index = view_order.subspan(static_cast<size_t>(grid_index), 1U).front();
  return {
    .col = view_index % cols,
    .row = view_index / cols,
  };
}

}// namespace vkgsplat
