#include "frame_context.hpp"

#include "app_state.hpp"
#include "backend/vulkan/imgui_overlay.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/types.hpp>

#include <cstddef>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

namespace vkgsplat {

namespace {

  [[nodiscard]] auto BuildGsFrameConstants(Init const &init, RenderData const &data,
    CameraSnapshot const &camera) -> GsFrameConstants
  {
    glm::vec3 const camera_pos{ camera.position };

    GsFrameConstants frame_gs{};
    frame_gs.project = {
      .view = camera.view,
      .projection = camera.projection,
      .viewport = { static_cast<float>(init.swapchain->extent().width),
        static_cast<float>(init.swapchain->extent().height) },
      .sh_degree = gs::kViewerShDegree,
      .pad0 = 0U,
      .camera_position = glm::vec4{ camera_pos, 0.0F },
    };

    frame_gs.bin = {
      .viewport = { init.swapchain->extent().width, init.swapchain->extent().height },
      .max_instances = data.max_bin_instances,
      .tile_size = gs::kTileSize,
      .instance_count_address = init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer),
    };

    frame_gs.sort = {
      .instance_count_address = init.gpu_allocator.get_buffer_device_address(data.instance_count_buffer),
      .radix_dispatch_address = init.gpu_allocator.get_buffer_device_address(data.radix_dispatch_buffer),
      .draw_indirect_address = init.gpu_allocator.get_buffer_device_address(data.draw_indirect_buffer),
      .sort_size = data.gaussian_sort_size,
      .tile_count = data.tile_count,
      .blocks_per_workgroup = data.radix_blocks_per_workgroup,
      .pad = 0U,
    };

    frame_gs.raster = {
      .camera_position = glm::vec4{ camera_pos, 0.0F },
      .viewport = { init.swapchain->extent().width, init.swapchain->extent().height },
      .tile_size = gs::kTileSize,
      .tiles_x = (init.swapchain->extent().width + gs::kTileSize - 1U) / gs::kTileSize,
      .sh_degree = gs::kViewerShDegree,
      .pad0 = 0U,
      .pad1 = 0U,
      .pad2 = 0U,
    };

    return frame_gs;
  }

}// namespace

auto SnapshotCamera(Camera const &camera, f64 aspect_ratio) -> CameraSnapshot
{
  return {
    .position = camera.position(),
    .view = camera.view_matrix(),
    .projection = camera.projection_matrix(aspect_ratio),
  };
}

void BuildFrameSetupCpu(RenderData &data, size_t frame_slot, PrepareFrameParams const &params)
{
  FrameSetup setup{
    .aspect_ratio = params.aspect_ratio,
    .image_index = 0,
    .camera = SnapshotCamera(*params.camera, params.aspect_ratio),
    .gs = {},
    .imgui = {},
  };

  BuildImGuiFrameSnapshot(data, frame_slot, setup.imgui);

  data.frames.at(frame_slot).setup = setup;
}

void BuildFrameSetupGpu(Init const &init, RenderData const &data, FrameSetup &setup, size_t image_index)
{
  setup.image_index = image_index;
  setup.gs = BuildGsFrameConstants(init, data, setup.camera);
}

void ApplyFrameSetup(RenderData &data, size_t frame_slot)
{
  FrameSetup const &setup = data.frames.at(frame_slot).setup;

  data.present_image_index = setup.image_index;
  data.project_push = setup.gs.project;
  data.bin_push = setup.gs.bin;
  data.sort_push = setup.gs.sort;
  data.raster_push = setup.gs.raster;
}

auto FrameSetupFor(RenderData const &data, size_t frame_slot) -> FrameSetup const &
{ return data.frames.at(frame_slot).setup; }

}// namespace vkgsplat
