#include "compute/op_fill_buffer.hpp"

#include "app_state.hpp"
#include "backend/vulkan/sync_objects/barrier.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

OpFillBuffer::OpFillBuffer(FillBufferParams params) : params_(params) {}

void OpFillBuffer::record(vulkan::Context const &context, [[maybe_unused]] RenderData const &data, VkCommandBuffer command_buffer)
{
  context.disp.cmdFillBuffer(command_buffer, params_.buffer, params_.offset, params_.size, params_.value);
  Barrier::transfer_to_compute(context.disp, command_buffer);
}

}// namespace vkgsplat::compute
