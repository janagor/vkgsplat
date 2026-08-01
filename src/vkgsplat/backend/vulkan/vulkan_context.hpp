#pragma once

#include <memory>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

#include "gpu_allocator.hpp"
#include "presentable_swapchain.hpp"
#include <vkgsplat/platform.hpp>

namespace vkgsplat {

// Hardware context for the Vulkan backend (Filament Driver equivalent state).
struct Init
{
  Platform *platform{};
  vkb::Instance instance{};
  vkb::InstanceDispatchTable inst_disp;
  VkSurfaceKHR surface{};
  vkb::Device device{};
  vkb::DispatchTable disp;
  std::unique_ptr<vulkan::PresentableSwapchain> swapchain;
  vulkan::GPUAllocator gpu_allocator;
  PFN_vkWriteResourceDescriptorsEXT write_resource_descriptors{};
  PFN_vkWriteSamplerDescriptorsEXT write_sampler_descriptors{};
  PFN_vkCmdBindResourceHeapEXT cmd_bind_resource_heap{};
  PFN_vkCmdBindSamplerHeapEXT cmd_bind_sampler_heap{};
  PFN_vkCmdPushDataEXT cmd_push_data{};
};

}// namespace vkgsplat

namespace vkgsplat::vulkan {

using Context = Init;

}// namespace vkgsplat::vulkan
