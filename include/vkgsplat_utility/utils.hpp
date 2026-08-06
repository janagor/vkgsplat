#ifndef VKGSPLAT_UTILITY_UTILS_HPP
#define VKGSPLAT_UTILITY_UTILS_HPP

#include <vkgsplat_utility/concepts.hpp>

#include <bit>
#include <climits>
#include <concepts>
#include <ranges>
#include <type_traits>

namespace vkgsplat {

template<std::ranges::range RangeTp> [[nodiscard]] constexpr auto AsSubrange(RangeTp &&range)
{ return std::ranges::subrange{ std::ranges::begin(range), std::ranges::end(range) }; }

template<std::integral Tp> [[nodiscard]] constexpr auto IsPowerOf2(Tp value) noexcept -> bool
{ return value > 0 && std::has_single_bit(static_cast<std::make_unsigned_t<Tp>>(value)); }

template<std::integral Tp> [[nodiscard]] constexpr auto IntFloorLog2(Tp value) noexcept -> Tp
{
  using Unsigned = std::make_unsigned_t<Tp>;
  return static_cast<Tp>(static_cast<int>(sizeof(Tp) * CHAR_BIT) - std::countl_zero(static_cast<Unsigned>(value)) - 1);
}

template<std::integral Tp> [[nodiscard]] constexpr auto IntCeilLog2(Tp value) noexcept -> Tp
{ return IntFloorLog2(value) + (IsPowerOf2(value) ? Tp{ 0 } : Tp{ 1 }); }

template<UnsignedIntegral Tp> [[nodiscard]] constexpr auto NextPowerOf2(Tp value) noexcept -> Tp
{
  if (value <= 1) { return Tp{ 1 }; }
  return static_cast<Tp>(Tp{ 1 } << IntCeilLog2(value));
}

}// namespace vkgsplat

#endif// VKGSPLAT_UTILITY_UTILS_HPP
