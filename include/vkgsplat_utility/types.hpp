#pragma once

#include <concepts>
#include <cstdint>
#include <limits>

namespace vkgsplat {

// NOLINTBEGIN(readability-identifier-naming)
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;
// NOLINTEND(readability-identifier-naming)

template<class Containing, class Contained>
concept ContainingMax = std::integral<Containing> && std::integral<Contained>
                        && (std::numeric_limits<Containing>::max() >= std::numeric_limits<Contained>::max());

template<class IntType>
  requires(ContainingMax<IntType, u8>)
static constexpr IntType kU8Max = UINT8_MAX;

template<class IntType>
  requires(ContainingMax<IntType, u16>)
static constexpr IntType kU16Max = UINT16_MAX;

template<class IntType>
  requires(ContainingMax<IntType, u32>)
static constexpr IntType kU32Max = UINT32_MAX;

template<class IntType>
  requires(ContainingMax<IntType, u64>)
static constexpr IntType kU64Max = UINT64_MAX;

template<class IntType>
  requires(ContainingMax<IntType, i8>)
static constexpr IntType kI8Max = INT8_MAX;

template<class IntType>
  requires(ContainingMax<IntType, i16>)
static constexpr IntType kI16Max = INT16_MAX;

template<class IntType>
  requires(ContainingMax<IntType, i32>)
static constexpr IntType kI32Max = INT32_MAX;

template<class IntType>
  requires(ContainingMax<IntType, i64>)
static constexpr IntType kI64Max = INT64_MAX;

template<class IntType> static constexpr IntType kZero = 0;

struct Extent2D
{
  u32 width = 0;
  u32 height = 0;

  [[nodiscard]] constexpr auto operator==(Extent2D const &) const noexcept -> bool = default;
};

}// namespace vkgsplat
