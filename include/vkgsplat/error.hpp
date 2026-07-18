#pragma once

#include <source_location>
#include <string>
#include <system_error>

namespace vkgsplat {

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

}// namespace vkgsplat
#include <vkgsplat/error.ipp>
