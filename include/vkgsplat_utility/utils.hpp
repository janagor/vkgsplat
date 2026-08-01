#pragma once

#include <vkgsplat_utility/concepts.hpp>

#include <bit>
#include <climits>
#include <concepts>
#include <ranges>
#include <type_traits>

namespace vkgsplat {

template<std::ranges::range RangeTp> [[nodiscard]] constexpr auto as_subrange(RangeTp &&range)
{ return std::ranges::subrange{ std::ranges::begin(range), std::ranges::end(range) }; }

template<std::integral Tp> [[nodiscard]] constexpr bool is_power_of_2(Tp value) noexcept
{ return value > 0 && std::has_single_bit(static_cast<std::make_unsigned_t<Tp>>(value)); }

template<std::integral Tp> [[nodiscard]] constexpr Tp int_floor_log2(Tp value) noexcept
{
  using Unsigned = std::make_unsigned_t<Tp>;
  return static_cast<Tp>(static_cast<int>(sizeof(Tp) * CHAR_BIT) - std::countl_zero(static_cast<Unsigned>(value)) - 1);
}

template<std::integral Tp> [[nodiscard]] constexpr Tp int_ceil_log2(Tp value) noexcept
{ return int_floor_log2(value) + (is_power_of_2(value) ? Tp{ 0 } : Tp{ 1 }); }

template<UnsignedIntegral Tp> [[nodiscard]] constexpr Tp next_power_of_2(Tp value) noexcept
{
  if (value <= 1) { return Tp{ 1 }; }
  return static_cast<Tp>(Tp{ 1 } << int_ceil_log2(value));
}

}// namespace vkgsplat
