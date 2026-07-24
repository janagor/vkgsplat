#include "gs/pipeline.hpp"

#include "app_state.hpp"
#include "compute/op_fill_buffer.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/operations.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/types.hpp>

#include <memory>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::gs {

void record_gs_pipeline(RenderData &data)
{
  auto const tile_ranges_size = static_cast<VkDeviceSize>(data.tile_count * sizeof(TileRange));

  data.gs_sequence.record(std::make_shared<OpProjection>())
    .record(std::make_shared<compute::OpFillBuffer>(compute::FillBufferParams{
      .buffer = data.instance_count_buffer.handle,
      .offset = 0,
      .size = sizeof(u32),
      .value = 0U,
    }))
    .record(std::make_shared<OpBinning>())
    .record(std::make_shared<OpPrepareSort>())
    .record(std::make_shared<OpRadixSort>())
    .record(std::make_shared<compute::OpFillBuffer>(compute::FillBufferParams{
      .buffer = data.tile_ranges_buffer.handle,
      .offset = 0,
      .size = tile_ranges_size,
      .value = 0U,
    }))
    .record(std::make_shared<OpIdentifyRanges>())
    .record(std::make_shared<OpRasterization>());
}

void update_gs_frame_state(Init const &init, RenderData &data, GsFrameParams const &frame)
{
  data.present_image_index = frame.image_index;

  data.project_push = {
    .view = frame.camera.view_matrix(),
    .projection = frame.camera.projection_matrix(frame.aspect_ratio),
    .viewport = { static_cast<float>(init.swapchain->extent().width),
      static_cast<float>(init.swapchain->extent().height) },
    .padding = {},
  };

  data.bin_push = {
    .viewport = { init.swapchain->extent().width, init.swapchain->extent().height },
    .max_instances = data.max_bin_instances,
    .tile_size = k_tile_size,
    .instance_count_address = init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer),
  };

  data.sort_push = {
    .instance_count_address = init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer),
    .sort_size = data.gaussian_sort_size,
    .tile_count = data.tile_count,
  };

  glm::vec3 const camera_pos{ frame.camera.position() };
  data.raster_push = {
    .camera_position = glm::vec4{ camera_pos, 0.0F },
    .viewport = { init.swapchain->extent().width, init.swapchain->extent().height },
    .tile_size = k_tile_size,
    .tiles_x = (init.swapchain->extent().width + k_tile_size - 1U) / k_tile_size,
    .sh_degree = 3U,
    .pad0 = 0U,
    .pad1 = 0U,
    .pad2 = 0U,
  };
}

void eval_gs_pipeline(Init &init, RenderData const &data, VkCommandBuffer command_buffer)
{ data.gs_sequence.eval(init, data, command_buffer); }

void destroy_gs_pipeline(RenderData &data) { data.gs_sequence.clear(); }

}// namespace vkgsplat::gs
