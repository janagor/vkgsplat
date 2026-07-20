#pragma once

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>

#include <vkgsplat/types.hpp>

namespace vkgsplat::gs {

// Stage 1 projection compute (view/proj + viewport in pixels).
struct ProjectPushConstants
{
  glm::mat4 view{};
  glm::mat4 projection{};
  glm::vec2 viewport{};// width, height
  glm::vec2 padding{};
};

static_assert(sizeof(ProjectPushConstants) == 144);

// Stage 2 tile binning.
struct BinPushConstants
{
  glm::uvec2 viewport{};// width, height in pixels
  u32 max_instances{};
  u32 tile_size{ 16 };
  u64 instance_count_address{};// BDA for atomic counter (Mesa heap atomics are broken)
};

static_assert(sizeof(BinPushConstants) == 24);

// Stage 3 prepare / identify passes.
struct SortPushConstants
{
  u64 instance_count_address{};
  u32 sort_size{};
  u32 tile_count{};
};

static_assert(sizeof(SortPushConstants) == 16);

// One bitonic compare-exchange stage (host loops over all (k, j) phases).
struct BitonicPushConstants
{
  u32 sort_size{};
  u32 k{};
  u32 j{};
  u32 pad{};
};

static_assert(sizeof(BitonicPushConstants) == 16);

// Stage 4 per-tile front-to-back rasterization.
// Layout matches GLSL std430 push_constant packing (vec3/vec4 alignment).
struct RasterPushConstants
{
  glm::vec4 camera_position{};// xyz used
  glm::uvec2 viewport{};// width, height in pixels
  u32 tile_size{ 16 };
  u32 tiles_x{};
  glm::vec4 background{ 0.02F, 0.02F, 0.05F, 0.0F };// rgb used
  u32 sh_degree{ 3 };// 0 = SH0 only; 3 = full degree-3
  u32 pad0{};
  u32 pad1{};
  u32 pad2{};
};

static_assert(sizeof(RasterPushConstants) == 64);

}// namespace vkgsplat::gs
