#pragma once

#include <span>

namespace vkgsplat {

[[nodiscard]] auto run(std::span<char *const> args) noexcept -> int;

}// namespace vkgsplat
