#include <vkgsplat/error.hpp>

#include <magic_enum/magic_enum.hpp>
#include <string>
#include <system_error>

namespace vkgsplat {

namespace detail {

  struct ErrorIOCategory : std::error_category
  {
    [[nodiscard]] auto name() const noexcept -> char const * override { return "vkgsplat_io"; }
    [[nodiscard]] auto message(int err) const -> std::string override { return to_string(static_cast<ErrorIO>(err)); }
  };

  ErrorIOCategory const kIoErrorCategory;

}// namespace detail

auto make_error_code(ErrorIO err) -> std::error_code { return { static_cast<int>(err), detail::kIoErrorCategory }; }

auto to_string(ErrorIO err) -> char const *
{
  if (auto const name = magic_enum::enum_name(err); !name.empty()) { return name.data(); }
  return "";
}

}// namespace vkgsplat
