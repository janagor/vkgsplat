#include "gs/pipeline.hpp"

#include "app_state.hpp"
#include "compute/op_fill_buffer.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/operations.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/types.hpp>

#include <cstddef>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

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

void UpdateGsFrameState(Init const &init, RenderData &data, GsFrameParams const &frame)
{
  data.present_image_index = frame.image_index;

  glm::vec3 const camera_pos{ frame.camera->position() };
  data.project_push = {
    .view = frame.camera->view_matrix(),
    .projection = frame.camera->projection_matrix(frame.aspect_ratio),
    .viewport = { static_cast<float>(init.swapchain->extent().width),
      static_cast<float>(init.swapchain->extent().height) },
    .sh_degree = kViewerShDegree,
    .pad0 = 0U,
    .camera_position = glm::vec4{ camera_pos, 0.0F },
  };

  data.bin_push = {
    .viewport = { init.swapchain->extent().width, init.swapchain->extent().height },
    .max_instances = data.max_bin_instances,
    .tile_size = kTileSize,
    .instance_count_address = init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer),
  };

  data.sort_push = {
    .instance_count_address = init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer),
    .radix_dispatch_address = init.gpu_allocator.get_buffer_device_address(data.radix_dispatch_buffer),
    .draw_indirect_address = init.gpu_allocator.get_buffer_device_address(data.draw_indirect_buffer),
    .sort_size = data.gaussian_sort_size,
    .tile_count = data.tile_count,
    .blocks_per_workgroup = data.radix_blocks_per_workgroup,
    .pad = 0U,
  };

  data.raster_push = {
    .camera_position = glm::vec4{ camera_pos, 0.0F },
    .viewport = { init.swapchain->extent().width, init.swapchain->extent().height },
    .tile_size = kTileSize,
    .tiles_x = (init.swapchain->extent().width + kTileSize - 1U) / kTileSize,
    .sh_degree = kViewerShDegree,
    .pad0 = 0U,
    .pad1 = 0U,
    .pad2 = 0U,
  };
}

void EvalGsPipeline(Init &init, RenderData &data, VkCommandBuffer command_buffer)
{
  size_t const slot = data.current_frame;
  if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.begin_frame(init, slot, command_buffer); }
  data.gs_sequence.eval(init, data, command_buffer);
  if (data.gpu_pass_timer.enabled()) { data.gpu_pass_timer.mark_submitted(slot); }
}

void DestroyGsPipeline(RenderData &data) { data.gs_sequence.clear(); }

}// namespace vkgsplat::gs
