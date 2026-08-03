#pragma once

#include <span>

namespace vkgsplat::app {

[[nodiscard]] auto Run(std::span<char *const> args) noexcept -> int;

}// namespace vkgsplat::app
