#ifndef VKGSPLAT_BACKEND_VULKAN_FRAME_CONTEXT_HPP
#define VKGSPLAT_BACKEND_VULKAN_FRAME_CONTEXT_HPP

#include "gs/push_constants.hpp"

#include <vkgsplat/camera.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>

namespace vkgsplat {

struct RenderData;

constexpr size_t kImGuiFpsLabelCapacity = 64;
constexpr size_t kImGuiGpuLabelCapacity = 256;

// CPU-side camera state copied once per frame slot (safe if the live Camera moves later).
struct CameraSnapshot
{
  glm::dvec3 position{};
  glm::dvec3 front{};
  glm::dvec3 right{};
  glm::dvec3 up{};
  f64 fov_degrees{ kCameraDefaultFovDegrees };
  f64 near_plane{ kCameraDefaultNearPlane };
  f64 far_plane{ kCameraDefaultFarPlane };
  glm::mat4 view{};
  glm::mat4 projection{};
};

[[nodiscard]] auto SnapshotCamera(Camera const &camera, f64 aspect_ratio) -> CameraSnapshot;

// Parallel-array off-axis view for one LFD quilt cell (same orientation as center).
struct QuiltView
{
  glm::mat4 view{};
  glm::mat4 projection{};
  glm::vec3 position{};
};

struct QuiltViewRequest
{
  CameraSnapshot const *center{};
  std::array<u32, 2> grid{ 1U, 1U };
  u32 col{};
  u32 row{};
  f64 view_cone_deg{};
  f64 focal_distance{};
  f64 tile_aspect{};
};

[[nodiscard]] auto MakeQuiltView(QuiltViewRequest const &request) -> QuiltView;

// GS push constants for one frame slot.
struct GsFrameConstants
{
  gs::ProjectPushConstants project{};
  gs::BinPushConstants bin{};
  gs::SortPushConstants sort{};
  gs::RasterPushConstants raster{};
};

// ImGui CPU snapshot for one frame slot (draw lists are built separately on the overlay).
struct ImGuiFrameSnapshot
{
  u64 ui_generation{};
  std::array<char, kImGuiFpsLabelCapacity> fps_label{};
  std::array<char, kImGuiGpuLabelCapacity> gpu_label{};
};

// Immutable-ish inputs for one in-flight frame before GPU recording.
struct FrameSetup
{
  f64 aspect_ratio{};
  size_t image_index{};
  CameraSnapshot camera{};
  GsFrameConstants gs{};
  ImGuiFrameSnapshot imgui{};
};

struct FrameContext
{
  FrameSetup setup{};
};

struct PrepareFrameParams
{
  Camera const *camera{};
  f64 aspect_ratio{};
};

namespace vulkan { struct Context; }

// Main thread: camera + ImGui snapshot (requires GLFW thread).
void BuildFrameSetupCpu(RenderData &data, size_t frame_slot, PrepareFrameParams const &params);

// Render thread: finalize setup after swapchain acquire.
void BuildFrameSetupGpu(vulkan::Context const &context, RenderData const &data, FrameSetup &setup, size_t image_index);

// Copy the slot's GS constants into RenderData for the existing pipeline ops.
void ApplyFrameSetup(RenderData &data, size_t frame_slot);

[[nodiscard]] auto FrameSetupFor(RenderData const &data, size_t frame_slot) -> FrameSetup const &;

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_FRAME_CONTEXT_HPP
