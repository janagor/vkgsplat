#ifndef VKGSPLAT_APP_APPLICATION_HPP
#define VKGSPLAT_APP_APPLICATION_HPP

#include <span>

namespace vkgsplat::app {

[[nodiscard]] auto Run(std::span<char *const> args) noexcept -> int;

}// namespace vkgsplat::app

#endif// VKGSPLAT_APP_APPLICATION_HPP
