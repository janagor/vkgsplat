#ifndef VKGSPLAT_BACKEND_VULKAN_VKEXEC_REQUIREMENTS_HPP
#define VKGSPLAT_BACKEND_VULKAN_VKEXEC_REQUIREMENTS_HPP

#include <vkexec/vulkan_requirements.hpp>

namespace vkgsplat::vulkan {

/// Device floors matching `DeviceInitialization` (API 1.4, descriptor heap, BDA,
/// untyped pointers, timeline, dynamic rendering). Present-timing stays optional and
/// surface-gated in vkgsplat - not part of these hard requirements.
[[nodiscard]] auto MakeVkexecRequirements() -> vkexec::vulkan_requirements;

}// namespace vkgsplat::vulkan

#endif// VKGSPLAT_BACKEND_VULKAN_VKEXEC_REQUIREMENTS_HPP
