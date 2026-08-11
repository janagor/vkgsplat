#ifndef VKGSPLAT_BACKEND_VULKAN_VULKAN_BOOTSTRAP_HPP
#define VKGSPLAT_BACKEND_VULKAN_VULKAN_BOOTSTRAP_HPP

#include <expected>
#include <ranges>
#include <string>

#include <vkgsplat_utility/error.hpp>

#include <VkBootstrap.h>

namespace vkgsplat {

template<typename Ok> auto VKBResultToExpected(vkb::Result<Ok> &&res) -> std::expected<Ok, Error>
{
  if (!res) {
    auto message = res.detailed_failure_reasons() | std::views::join_with('\n') | std::ranges::to<std::string>();
    return std::unexpected{ Error{ res.error(), message } };
  }
  return std::move(res).value();
}

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_VULKAN_BOOTSTRAP_HPP
