#pragma once

#include <concepts>
#include <ranges>
#include <type_traits>
#include <utility>

namespace vkgsplat {

template<typename Tp>
concept TriviallyCopyable = std::is_trivially_copyable_v<Tp>;

template<typename Range, typename Tp>
concept InputRange =
  std::ranges::input_range<Range> && std::same_as<std::ranges::range_value_t<Range>, Tp>;

template<template<typename...> class Template, class Tp>
class IsSpecialization
{
  template<typename... Args>
  static constexpr auto helper(Template<Args...> const &) -> std::true_type;

  static constexpr auto helper(...) -> std::false_type;

public:
  static constexpr bool value =
    std::same_as<decltype(helper(std::declval<Tp>())), std::true_type>;
};

template<template<typename...> class Template, class Tp>
inline constexpr bool IsSpecializationV = IsSpecialization<Template, Tp>::value;

template<typename Tp, template<typename...> class Template>
concept SpecializationOf = IsSpecializationV<Template, Tp>;

template<typename Tp>
concept UnsignedIntegral = std::integral<Tp> && std::is_unsigned_v<Tp>;

template<typename FuncTp, typename ReturnTp, typename... ArgsTp>
concept Invocable = std::invocable<FuncTp, ArgsTp...>
                    && std::convertible_to<std::invoke_result_t<FuncTp, ArgsTp...>, ReturnTp>;

}// namespace vkgsplat
