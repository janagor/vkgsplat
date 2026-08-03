#pragma once

#include <vkgsplat_utility/error.hpp>// NOLINT(misc-header-include-cycle)

#include <utility>

namespace vkgsplat {

template<class ErrorCodeEnum>
  requires std::is_error_code_enum_v<ErrorCodeEnum>
auto MakeError(ErrorCodeEnum code, std::string message, std::source_location source_location) -> Error
{
  return MakeError(make_error_code(code), std::move(message), source_location);
}

}// namespace vkgsplat
