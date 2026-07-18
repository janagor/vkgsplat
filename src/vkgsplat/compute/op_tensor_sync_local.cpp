#include "compute/op_tensor_sync_local.hpp"

#include "app_state.hpp"
#include "vulkan_context.hpp"

#include <print>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

void OpTensorSyncLocal::post_eval(Init &init, RenderData const &data, VkCommandBuffer cmd)
{
  for (auto const &sync : syncs_) {
    if (!sync(init)) { std::println("OpTensorSyncLocal: failed to sync tensor from device"); }
  }

  (void)data;
  (void)cmd;
}

}// namespace vkgsplat::compute
