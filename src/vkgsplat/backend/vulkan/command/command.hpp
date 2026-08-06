#ifndef VKGSPLAT_BACKEND_VULKAN_COMMAND_COMMAND_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMMAND_COMMAND_HPP

#include <concepts>
#include <expected>
#include <functional>
#include <system_error>
#include <utility>

#include "backend/vulkan/initializers.hpp"
#include <vkgsplat_utility/error.hpp>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

template<typename RecordFn>
  requires std::invocable<RecordFn, vkb::DispatchTable &, VkCommandBuffer>
auto WithCommand(std::reference_wrapper<vkb::DispatchTable> disp,
  VkCommandBuffer command_buffer,
  RecordFn &&record,
  VkCommandBufferUsageFlags flags = 0) -> std::expected<void, Error>
{
  auto const begin_info = initializers::CommandBufferBeginInfo(flags);
  auto &dispatch = disp.get();

  if (dispatch.beginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::io_error), "Failed to begin command buffer" } };
  }

  std::forward<RecordFn>(record)(dispatch, command_buffer);

  if (dispatch.endCommandBuffer(command_buffer) != VK_SUCCESS) {
    return std::unexpected{ Error{ std::make_error_code(std::errc::io_error), "Failed to end command buffer" } };
  }

  return {};
}

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_COMMAND_COMMAND_HPP
