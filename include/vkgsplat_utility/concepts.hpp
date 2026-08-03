#pragma once

#include <concepts>
#include <ranges>
#include <type_traits>

namespace vkgsplat {

template<typename Tp>
concept TriviallyCopyable = std::is_trivially_copyable_v<Tp>;

template<typename Range, typename Tp>
concept InputRange = std::ranges::input_range<Range> && std::same_as<std::ranges::range_value_t<Range>, Tp>;

template<typename Tp>
concept UnsignedIntegral = std::integral<Tp> && std::is_unsigned_v<Tp>;

}// namespace vkgsplat
