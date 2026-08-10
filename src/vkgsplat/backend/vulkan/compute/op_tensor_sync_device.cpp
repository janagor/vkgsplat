#include "compute/op_tensor_sync_device.hpp"

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <print>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

void OpTensorSyncDevice::pre_eval(vulkan::Context &context, RenderData const &data, VkCommandBuffer cmd)
{
  for (auto const &sync : syncs_) {
    if (!sync(context)) { std::println("OpTensorSyncDevice: failed to sync tensor to device"); }
  }

  (void)data;
  (void)cmd;
}

}// namespace vkgsplat::compute
