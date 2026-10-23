#ifndef VKGSPLAT_BACKEND_VULKAN_VKEXEC_ERROR_HPP
#define VKGSPLAT_BACKEND_VULKAN_VKEXEC_ERROR_HPP

#include <system_error>

#include <vkexec/error.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

[[nodiscard]] inline auto VkexecFailure(vkexec::error const &failure) -> Error
{
  auto const device_lost =
    failure.code.category() == vkexec::vulkan_category() && failure.code.value() == VK_ERROR_DEVICE_LOST;
  return MakeError(device_lost ? std::errc::no_such_device : std::errc::io_error, failure.message());
}

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_VKEXEC_ERROR_HPP
