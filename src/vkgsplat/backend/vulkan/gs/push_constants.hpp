#pragma once

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::gs {

// Stage 1 projection compute (view/proj + viewport in pixels).
struct ProjectPushConstants
{
  glm::mat4 view{};
  glm::mat4 projection{};
  glm::vec2 viewport{};// width, height
  u32 sh_degree{ 3 };// 0 = SH0 only (fast viewer); 3 = full view-dependent SH
  u32 pad0{};
  glm::vec4 camera_position{};// xyz used for SH view direction
};

constexpr size_t kProjectPushConstantsSize = 160;
static_assert(sizeof(ProjectPushConstants) == kProjectPushConstantsSize);

// Stage 2 tile binning.
struct BinPushConstants
{
  glm::uvec2 viewport{};// width, height in pixels
  u32 max_instances{};
  u32 tile_size{ 16 };// NOLINT(readability-magic-numbers)
  u64 instance_count_address{};// BDA for atomic counter (Mesa heap atomics are broken)
};

constexpr size_t kBinPushConstantsSize = 24;
static_assert(sizeof(BinPushConstants) == kBinPushConstantsSize);

// Stage 3 prepare / identify passes.
struct SortPushConstants
{
  u64 instance_count_address{};
  u64 radix_dispatch_address{};// VkDispatchIndirectCommand {x,y,z}
  u64 draw_indirect_address{};// VkDrawIndirectCommand
  u32 sort_size{};// capacity
  u32 tile_count{};
  u32 blocks_per_workgroup{ 32 };// NOLINT(readability-magic-numbers)
  u32 pad{};
};

constexpr size_t kSortPushConstantsSize = 40;
static_assert(sizeof(SortPushConstants) == kSortPushConstantsSize);

// Multi-pass radix sort. Keys are packed uint32 (tile<<16)|(depth>>16); 4× 8-bit passes.
struct RadixPushConstants
{
  u64 instance_count_address{};
  u32 capacity{};
  u32 shift{};
  u32 num_blocks_per_workgroup{};
  u32 ping{};// 0: sorted→unsorted, 1: unsorted→sorted
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
  u32 tile_size{ 16 };// NOLINT(readability-magic-numbers)
  u32 tiles_x{};
  glm::vec4 background{ 0.0F, 0.0F, 0.0F, 0.0F };// rgb used
  u32 sh_degree{ 3 };// 0 = SH0 only; 3 = full degree-3
  u32 pad0{};
  u32 pad1{};
  u32 pad2{};
};

constexpr size_t kRasterPushConstantsSize = 64;
static_assert(sizeof(RasterPushConstants) == kRasterPushConstantsSize);

}// namespace vkgsplat::gs
