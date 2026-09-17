#ifndef VKGSPLAT_BACKEND_VULKAN_VULKAN_CONTEXT_HPP
#define VKGSPLAT_BACKEND_VULKAN_VULKAN_CONTEXT_HPP

#include <memory>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>
#include <vkexec/context.hpp>

#include "presentable_swapchain.hpp"
#include <vkgsplat/platform.hpp>

namespace vkgsplat::vulkan {

// Hardware context for the Vulkan backend (Filament Driver equivalent state).
struct Context
{
  Platform *platform{};
  vkb::Instance instance{};
  vkb::InstanceDispatchTable inst_disp;
  VkSurfaceKHR surface{};
  vkb::Device device{};
  vkb::DispatchTable disp;
  /// Adopted vkexec context: owns VMA; does not own instance/device. Destroy before device.
  std::unique_ptr<vkexec::context> vkexec_context;
  std::unique_ptr<PresentableSwapchain> swapchain;

  bool present_timing_enabled = false;
  bool present_id2_enabled = false;
  bool present_at_absolute_time = false;
  bool present_at_relative_time = false;
  VkPresentStageFlagsEXT present_stage_queries = 0;
  PFN_vkSetSwapchainPresentTimingQueueSizeEXT set_swapchain_present_timing_queue_size{};
  PFN_vkGetSwapchainTimingPropertiesEXT get_swapchain_timing_properties{};
  PFN_vkGetSwapchainTimeDomainPropertiesEXT get_swapchain_time_domain_properties{};
  PFN_vkGetPastPresentationTimingEXT get_past_presentation_timing{};
};

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_VULKAN_CONTEXT_HPP
