#pragma once

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

enum class ErrorIO {
  kInvalidSplatCount,
  kFailedOpen,
  kMissingVertexElement,
  kEmptyVertexElement,
  kMissingProperty,
  kExtractPropertyFailed,
  kLoadElementFailed,
};
 //NOLINTNEXTLINE(readability-identifier-naming)
[[nodiscard]] auto make_error_code(ErrorIO err) -> std::error_code;
[[nodiscard]] auto ToString(ErrorIO err) -> char const *;

}// namespace vkgsplat

namespace std {
template<> struct is_error_code_enum<vkgsplat::ErrorIO> : true_type
{
};
}// namespace std
