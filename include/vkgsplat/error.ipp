#pragma once

#include <vkgsplat/error.hpp>// NOLINT(misc-header-include-cycle)

#include <source_location>
#include <string>
#include <system_error>
#include <utility>

namespace vkgsplat {

namespace detail {

struct ErrorIOCategory : std::error_category
{
  [[nodiscard]] auto name() const noexcept -> char const * override { return "vkgsplat_io"; }
  [[nodiscard]] auto message(int err) const -> std::string override
  { return to_string(static_cast<ErrorIO>(err)); }
};

inline ErrorIOCategory const io_error_category;

}// namespace detail

constexpr Error::Error(std::error_code error_code, std::string message, std::source_location source_location)
  : code_(error_code), message_(std::move(message)), source_location_(source_location)
{}

constexpr auto Error::code() const noexcept -> std::error_code const & { return code_; }
constexpr auto Error::message() const noexcept -> std::string const & { return message_; }
constexpr auto Error::location() const noexcept -> std::source_location const & { return source_location_; }

inline auto make_error_code(ErrorIO err) -> std::error_code
{ return { static_cast<int>(err), detail::io_error_category }; }

inline auto to_string(ErrorIO err) -> char const *
{
  switch (err) {
  case ErrorIO::invalid_splat_count: return "invalid_splat_count";
  case ErrorIO::failed_open: return "failed_open";
  case ErrorIO::missing_vertex_element: return "missing_vertex_element";
  case ErrorIO::empty_vertex_element: return "empty_vertex_element";
  case ErrorIO::missing_property: return "missing_property";
  case ErrorIO::extract_property_failed: return "extract_property_failed";
  case ErrorIO::load_element_failed: return "load_element_failed";
  default: return "";
  }
}

inline auto make_error(ErrorIO code, std::string message, std::source_location source_location) -> Error
{ return Error{ make_error_code(code), std::move(message), source_location }; }

}// namespace vkgsplat
