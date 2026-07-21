#pragma once

#include <source_location>
#include <string>
#include <system_error>

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

class Error
{
public:
  explicit constexpr Error(std::error_code error_code,
    std::string message = "",
    std::source_location source_location = std::source_location::current());

  [[nodiscard]] constexpr auto code() const noexcept -> std::error_code const &;
  [[nodiscard]] constexpr auto message() const noexcept -> std::string const &;
  [[nodiscard]] constexpr auto location() const noexcept -> std::source_location const &;

private:
  std::error_code code_;
  std::string message_;
  std::source_location source_location_;
};

[[nodiscard]] auto make_error(ErrorIO code,
  std::string message = "",
  std::source_location source_location = std::source_location::current()) -> Error;

}// namespace vkgsplat

namespace std {
template <> struct is_error_code_enum<vkgsplat::ErrorIO> : true_type {};
}// namespace std

#include <vkgsplat/error.ipp>
