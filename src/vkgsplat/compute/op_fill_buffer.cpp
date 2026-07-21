#include "compute/op_fill_buffer.hpp"

#include "app_state.hpp"
#include "compute/vulkan_barrier.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

OpFillBuffer::OpFillBuffer(FillBufferParams params) : params_(params) {}

void OpFillBuffer::record(Init const &init,
  [[maybe_unused]] RenderData const &data,
  VkCommandBuffer command_buffer)
{
  init.disp.cmdFillBuffer(command_buffer, params_.buffer, params_.offset, params_.size, params_.value);
  transfer_to_compute_barrier(init, command_buffer);
}

}// namespace vkgsplat::compute
