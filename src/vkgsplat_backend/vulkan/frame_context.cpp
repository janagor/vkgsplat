#include "frame_context.hpp"

#include "app_state.hpp"
#include "vulkan/gpu_buffers.hpp"
#include "vulkan/imgui_overlay.hpp"
#include "gs/gaussian_splat.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/trigonometric.hpp>

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace {

  constexpr f64 kQuiltNormalizedCenter = 0.5;

  [[nodiscard]] auto TileExtent(vulkan::Context const &context, RenderData const &data) -> VkExtent2D
  {
    if (data.quilt_tile_extent.width != 0U && data.quilt_tile_extent.height != 0U) {
      return data.quilt_tile_extent;
    }
    return context.swapchain->vk_extent();
  }

  [[nodiscard]] auto BuildGsFrameConstants(vulkan::Context const &context, RenderData const &data,
    CameraSnapshot const &camera) -> GsFrameConstants
  {
    glm::vec3 const camera_pos{ camera.position };
    VkExtent2D const tile = TileExtent(context, data);
    f32 const tile_w = static_cast<f32>(tile.width);
    f32 const tile_h = static_cast<f32>(tile.height);

    GsFrameConstants frame_gs{};
    frame_gs.project = {
      .view = camera.view,
      .projection = camera.projection,
      .viewport = { tile_w, tile_h },
      .sh_degree = gs::kViewerShDegree,
      .cull_margin = gs::kDefaultProjectionCullMargin,
      .camera_position = glm::vec4{ camera_pos, 0.0F },
    };

    frame_gs.bin = {
      .viewport = { tile.width, tile.height },
      .max_instances = data.max_bin_instances,
      .tile_size = gs::kTileSize,
      .instance_count_address = vulkan::DeviceAddressOrZero(data.instance_count_buffer),
    };

    frame_gs.sort = {
      .instance_count_address = vulkan::DeviceAddressOrZero(data.instance_count_buffer),
      .radix_dispatch_address = vulkan::DeviceAddressOrZero(data.radix_dispatch_buffer),
      .draw_indirect_address = vulkan::DeviceAddressOrZero(data.draw_indirect_buffer),
      .sort_size = data.gaussian_sort_size,
      .tile_count = data.tile_count,
      .blocks_per_workgroup = data.radix_blocks_per_workgroup,
      .pad = 0U,
    };

    frame_gs.raster = {
      .camera_position = glm::vec4{ camera_pos, 0.0F },
      .viewport = { tile.width, tile.height },
      .tile_size = gs::kTileSize,
      .tiles_x = (tile.width + gs::kTileSize - 1U) / gs::kTileSize,
      .sh_degree = gs::kViewerShDegree,
      .pad0 = 0U,
      .tile_offset = { 0U, 0U },
    };

    return frame_gs;
  }

}// namespace

auto SnapshotCamera(Camera const &camera, f64 aspect_ratio) -> CameraSnapshot
{
  return {
    .position = camera.position(),
    .front = camera.front(),
    .right = camera.right(),
    .up = camera.up(),
    .fov_degrees = camera.fov_degrees(),
    .near_plane = camera.near_plane(),
    .far_plane = camera.far_plane(),
    .view = camera.view_matrix(),
    .projection = camera.projection_matrix(aspect_ratio),
  };
}

auto MakeQuiltView(QuiltViewRequest const &request) -> QuiltView
{
  CameraSnapshot const &center = *request.center;

  f64 normalized_col = 0.0;
  f64 normalized_row = 0.0;
  if (request.grid.at(0) > 1U) {
    normalized_col =
      (static_cast<f64>(request.col) / static_cast<f64>(request.grid.at(0) - 1U)) - kQuiltNormalizedCenter;
  }
  if (request.grid.at(1) > 1U) {
    normalized_row =
      (static_cast<f64>(request.row) / static_cast<f64>(request.grid.at(1) - 1U)) - kQuiltNormalizedCenter;
  }

  f64 const cone_h = request.view_cone_deg;
  f64 const cone_v = request.view_cone_deg / std::max(request.tile_aspect, 1e-6);
  f64 const focal = std::max(request.focal_distance, 1e-3);

  glm::dvec3 const offset = center.right * (std::tan(glm::radians(normalized_col * cone_h)) * focal)
                            // Row 0 is the top quilt tile (smaller framebuffer y); positive row index moves down
                            // the atlas, so the camera should shift opposite to world up for lower rows.
                            + center.up * (std::tan(glm::radians(-normalized_row * cone_v)) * focal);
  glm::dvec3 const eye = center.position + offset;
  glm::dvec3 const target = eye + center.front;

  glm::mat4 const view = glm::lookAt(glm::vec3{ eye }, glm::vec3{ target }, glm::vec3{ center.up });

  auto proj = glm::perspective(glm::radians(static_cast<float>(center.fov_degrees)),
    static_cast<float>(request.tile_aspect),
    static_cast<float>(center.near_plane),
    static_cast<float>(center.far_plane));
  proj[1][1] *= -1.0F;// NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

  return {
    .view = view,
    .projection = proj,
    .position = glm::vec3{ eye },
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

void BuildFrameSetupGpu(vulkan::Context const &context, RenderData const &data, FrameSetup &setup, size_t image_index)
{
  setup.image_index = image_index;
  setup.gs = BuildGsFrameConstants(context, data, setup.camera);
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
