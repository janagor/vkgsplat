#include "gs/pipeline.hpp"

#include "app_state.hpp"
#include "compute/op_fill_buffer.hpp"
#include "gs/operations.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_utility/types.hpp>

#include <cstddef>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

void RecordGsPipeline(RenderData &data)
{
  data.gs_sequence.emplace<OpProjection>()
    .emplace<compute::OpFillBuffer>(compute::FillBufferParams{
      .buffer = data.instance_count_buffer.handle,
      .offset = 0,
      .size = sizeof(u32),
      .value = 0U,
    })
    .emplace<OpBinning>()
    .emplace<OpPrepareSort>()
    .emplace<OpRadixSort>()
    .emplace<OpRasterization>();
}

void EvalGsPipeline(vulkan::Context &context, RenderData &data, VkCommandBuffer command_buffer)
{
  size_t const slot = data.current_frame;
  if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.begin_frame(context, slot, command_buffer); }
  data.gs_sequence.eval(context, data, command_buffer);
  if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.mark_submitted(slot); }
}

void DestroyGsPipeline(RenderData &data) { data.gs_sequence.clear(); }

}// namespace vkgsplat::gs
