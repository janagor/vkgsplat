#include <vkgsplat/error.hpp>

#include <magic_enum/magic_enum.hpp>
#include <source_location>
#include <string>
#include <system_error>
#include <utility>

namespace vkgsplat {

namespace detail {

  struct ErrorIOCategory : std::error_category
  {
    [[nodiscard]] auto name() const noexcept -> char const * override { return "vkgsplat_io"; }
    [[nodiscard]] auto message(int err) const -> std::string override { return to_string(static_cast<ErrorIO>(err)); }
  };

  ErrorIOCategory const io_error_category;

}// namespace detail

Error::Error(std::error_code error_code, std::string message, std::source_location source_location)
  : code_(error_code), message_(std::move(message)), source_location_(source_location)
{}

auto Error::code() const noexcept -> std::error_code const & { return code_; }
auto Error::message() const noexcept -> std::string const & { return message_; }
auto Error::location() const noexcept -> std::source_location const & { return source_location_; }

auto make_error_code(ErrorIO err) -> std::error_code { return { static_cast<int>(err), detail::io_error_category }; }

auto to_string(ErrorIO err) -> char const *
{
  if (auto const name = magic_enum::enum_name(err); !name.empty()) { return name.data(); }
  return "";
}

auto make_error(ErrorIO code, std::string message, std::source_location source_location) -> Error
{ return Error{ make_error_code(code), std::move(message), source_location }; }

}// namespace vkgsplat
