#ifndef VKGSPLAT_BACKEND_VULKAN_GS_PUSH_CONSTANTS_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_PUSH_CONSTANTS_HPP

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>

#include <cstddef>

#include "gs/gaussian_splat.hpp"
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::gs {

// Stage 1 projection compute (view/proj + viewport in pixels).
struct ProjectPushConstants
{
  glm::mat4 view{};
  glm::mat4 projection{};
  glm::vec2 viewport{};// width, height
  u32 sh_degree{ 3 };// 0 = SH0 only (fast viewer); 3 = full view-dependent SH
  // NDC xy cull half-extent (1.0 = frustum; >1 keeps fringe for LFD sort-once).
  f32 cull_margin{ kDefaultProjectionCullMargin };
  glm::vec4 camera_position{};// xyz used for SH view direction
};

constexpr size_t kProjectPushConstantsSize = 160;
constexpr size_t kProjectProjectionOffset = sizeof(glm::mat4);
constexpr size_t kProjectViewportOffset = kProjectProjectionOffset + sizeof(glm::mat4);
constexpr size_t kProjectShDegreeOffset = kProjectViewportOffset + sizeof(glm::vec2);
constexpr size_t kProjectCullMarginOffset = kProjectShDegreeOffset + sizeof(u32);
constexpr size_t kProjectCameraPositionOffset = kProjectCullMarginOffset + sizeof(f32);
static_assert(sizeof(ProjectPushConstants) == kProjectPushConstantsSize);
static_assert(offsetof(ProjectPushConstants, view) == 0U);
static_assert(offsetof(ProjectPushConstants, projection) == kProjectProjectionOffset);
static_assert(offsetof(ProjectPushConstants, viewport) == kProjectViewportOffset);
static_assert(offsetof(ProjectPushConstants, sh_degree) == kProjectShDegreeOffset);
static_assert(offsetof(ProjectPushConstants, cull_margin) == kProjectCullMarginOffset);
static_assert(offsetof(ProjectPushConstants, camera_position) == kProjectCameraPositionOffset);

struct ProjectBatchPushConstants
{
  u32 group_count_x{};
};

// Stage 2 tile binning.
struct BinPushConstants
{
  glm::uvec2 viewport{};// width, height in pixels
  u32 max_instances{};
  u32 tile_size{ kTileSize };
  u64 instance_count_address{};// BDA for atomic counter (Mesa heap atomics are broken)
};

constexpr size_t kBinPushConstantsSize = 24;
static_assert(sizeof(BinPushConstants) == kBinPushConstantsSize);
static_assert(offsetof(BinPushConstants, viewport) == 0U);
static_assert(offsetof(BinPushConstants, max_instances) == sizeof(glm::uvec2));
static_assert(offsetof(BinPushConstants, tile_size) == sizeof(glm::uvec2) + sizeof(u32));
static_assert(offsetof(BinPushConstants, instance_count_address) == 2U * sizeof(glm::uvec2));

// Stage 3 prepare / identify passes.
struct SortPushConstants
{
  u64 instance_count_address{};
  u64 radix_dispatch_address{};// VkDispatchIndirectCommand {x,y,z}
  u64 draw_indirect_address{};// VkDrawIndirectCommand
  u32 sort_size{};// capacity
  u32 tile_count{};
  u32 blocks_per_workgroup{ kRadixBlocksPerWorkgroup };
  u32 pad{};
};

constexpr size_t kSortPushConstantsSize = 40;
static_assert(sizeof(SortPushConstants) == kSortPushConstantsSize);
static_assert(offsetof(SortPushConstants, instance_count_address) == 0U);
static_assert(offsetof(SortPushConstants, radix_dispatch_address) == sizeof(u64));
static_assert(offsetof(SortPushConstants, draw_indirect_address) == 2U * sizeof(u64));
static_assert(offsetof(SortPushConstants, sort_size) == 3U * sizeof(u64));
static_assert(offsetof(SortPushConstants, tile_count) == (3U * sizeof(u64)) + sizeof(u32));
static_assert(offsetof(SortPushConstants, blocks_per_workgroup) == (3U * sizeof(u64)) + (2U * sizeof(u32)));
static_assert(offsetof(SortPushConstants, pad) == (3U * sizeof(u64)) + (3U * sizeof(u32)));

// Multi-pass radix sort. Keys are packed uint32 (tile<<16)|(depth>>16); 4 x 8-bit passes.
struct RadixPushConstants
{
  u64 instance_count_address{};
  u32 capacity{};
  u32 shift{};
  u32 num_blocks_per_workgroup{};
  u32 ping{};// 0: sorted -> unsorted, 1: unsorted -> sorted
  u32 pad0{};
  u32 pad1{};
};

constexpr size_t kRadixPushConstantsSize = 32;
static_assert(sizeof(RadixPushConstants) == kRadixPushConstantsSize);

// Stage 4 per-tile front-to-back rasterization.
// Layout matches GLSL std430 push_constant packing (vec3/vec4 alignment).
struct RasterPushConstants
{
  glm::vec4 camera_position{};// xyz used
  glm::uvec2 viewport{};// width, height in pixels
  u32 tile_size{ kTileSize };
  u32 tiles_x{};
  glm::vec4 background{ 0.0F, 0.0F, 0.0F, 0.0F };// rgb used
  u32 projected_splat_count{};
  u32 projected_view_index{};
  // Quilt tile origin in the atlas (framebuffer pixels); mono uses (0,0).
  glm::uvec2 tile_offset{};
};

constexpr size_t kRasterPushConstantsSize = 64;
static_assert(sizeof(RasterPushConstants) == kRasterPushConstantsSize);
static_assert(offsetof(RasterPushConstants, camera_position) == 0U);
static_assert(offsetof(RasterPushConstants, viewport) == sizeof(glm::vec4));
static_assert(offsetof(RasterPushConstants, tile_size) == sizeof(glm::vec4) + sizeof(glm::uvec2));
static_assert(offsetof(RasterPushConstants, tiles_x) == sizeof(glm::vec4) + sizeof(glm::uvec2) + sizeof(u32));
static_assert(offsetof(RasterPushConstants, background) == 2U * sizeof(glm::vec4));
static_assert(offsetof(RasterPushConstants, projected_splat_count) == 3U * sizeof(glm::vec4));
static_assert(offsetof(RasterPushConstants, projected_view_index) == (3U * sizeof(glm::vec4)) + sizeof(u32));
static_assert(offsetof(RasterPushConstants, tile_offset) == (3U * sizeof(glm::vec4)) + (2U * sizeof(u32)));

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_PUSH_CONSTANTS_HPP
