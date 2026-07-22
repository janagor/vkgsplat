#pragma once

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

enum class ErrorIO {
  invalid_splat_count,
  failed_open,
  missing_vertex_element,
  empty_vertex_element,
  missing_property,
  extract_property_failed,
  load_element_failed,
};

[[nodiscard]] auto make_error_code(ErrorIO err) -> std::error_code;
[[nodiscard]] auto to_string(ErrorIO err) -> char const *;

}// namespace vkgsplat

namespace std {
template<> struct is_error_code_enum<vkgsplat::ErrorIO> : true_type
{
};
}// namespace std
