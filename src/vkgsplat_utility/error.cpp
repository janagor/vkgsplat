#include <vkgsplat_utility/error.hpp>

#include <source_location>
#include <string>
#include <system_error>
#include <utility>

namespace vkgsplat {

Error::Error(std::error_code error_code, std::string message, std::source_location source_location)
  : code_(error_code), message_(std::move(message)), source_location_(source_location)
{}

auto Error::code() const noexcept -> std::error_code const & { return code_; }
auto Error::message() const noexcept -> std::string const & { return message_; }
auto Error::location() const noexcept -> std::source_location const & { return source_location_; }

auto MakeError(std::error_code code, std::string message, std::source_location source_location) -> Error
{ return Error{ code, std::move(message), source_location }; }

auto MakeError(std::errc code, std::string message, std::source_location source_location) -> Error
{ return MakeError(std::make_error_code(code), std::move(message), source_location); }

}// namespace vkgsplat
