#pragma once

#include <source_location>
#include <string>
#include <system_error>
#include <type_traits>

namespace vkgsplat {

class Error
{
public:
  explicit Error(std::error_code error_code,
    std::string message = "",
    std::source_location source_location = std::source_location::current());

  [[nodiscard]] auto code() const noexcept -> std::error_code const &;
  [[nodiscard]] auto message() const noexcept -> std::string const &;
  [[nodiscard]] auto location() const noexcept -> std::source_location const &;

private:
  std::error_code code_;
  std::string message_;
  std::source_location source_location_;
};

[[nodiscard]] auto MakeError(std::error_code code,
  std::string message = "",
  std::source_location source_location = std::source_location::current()) -> Error;

[[nodiscard]] auto MakeError(std::errc code,
  std::string message = "",
  std::source_location source_location = std::source_location::current()) -> Error;

template<class ErrorCodeEnum>
  requires std::is_error_code_enum_v<ErrorCodeEnum>
[[nodiscard]] auto MakeError(ErrorCodeEnum code,
  std::string message = "",
  std::source_location source_location = std::source_location::current()) -> Error;

}// namespace vkgsplat

#include <vkgsplat_utility/error.ipp>// IWYU pragma: export
