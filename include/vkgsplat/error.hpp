#ifndef VKGSPLAT_ERROR_HPP
#define VKGSPLAT_ERROR_HPP

#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

enum class ErrorIO : u8 {
  kFailedOpen,
  kMissingVertexElement,
  kEmptyVertexElement,
  kMissingProperty,
  kExtractPropertyFailed,
  kLoadElementFailed,
};
// NOLINTNEXTLINE(readability-identifier-naming)
[[nodiscard]] auto make_error_code(ErrorIO err) -> std::error_code;
[[nodiscard]] auto ToString(ErrorIO err) -> char const *;

}// namespace vkgsplat

namespace std {
template<> struct is_error_code_enum<vkgsplat::ErrorIO> : true_type
{
};
}// namespace std

#endif// VKGSPLAT_ERROR_HPP
