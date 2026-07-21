#pragma once

#include <span>

namespace vkgsplat::app {

[[nodiscard]] auto run(std::span<char *const> args) noexcept -> int;

}// namespace vkgsplat::app
