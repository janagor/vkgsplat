#pragma once

#include <vkgsplat/error.hpp>// NOLINT(misc-header-include-cycle)

#include <source_location>
#include <string>
#include <system_error>

namespace vkgsplat {

constexpr Error::Error(std::error_code error_code, std::string message, std::source_location source_location)
  : code_(error_code), message_(std::move(message)), source_location_(source_location)
{}

constexpr auto Error::code() const noexcept -> std::error_code const & { return code_; }
constexpr auto Error::message() const noexcept -> std::string const & { return message_; }
constexpr auto Error::location() const noexcept -> std::source_location const & { return source_location_; }


}// namespace vkgsplat
