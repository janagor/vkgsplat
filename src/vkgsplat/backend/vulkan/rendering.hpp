#pragma once

#include <concepts>
#include <functional>
#include <utility>

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::vulkan {

template<typename DrawFn>
  requires std::invocable<DrawFn, vkb::DispatchTable &, VkCommandBuffer>
void with_rendering(std::reference_wrapper<vkb::DispatchTable> disp,
  VkCommandBuffer command_buffer,
  VkRenderingInfo const &rendering_info,
  DrawFn &&draw)
{
  auto &dispatch = disp.get();
  dispatch.cmdBeginRendering(command_buffer, &rendering_info);
  std::forward<DrawFn>(draw)(dispatch, command_buffer);
  dispatch.cmdEndRendering(command_buffer);
}

}// namespace vkgsplat::vulkan
