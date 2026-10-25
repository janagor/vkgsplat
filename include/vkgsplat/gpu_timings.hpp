#ifndef VKGSPLAT_GPU_TIMINGS_HPP
#define VKGSPLAT_GPU_TIMINGS_HPP

#include <vkgsplat_utility/types.hpp>

#include <array>
#include <cstddef>
#include <optional>

namespace vkgsplat {

enum class GpuPass : u8 {
  kProjection = 0,
  kBinning,
  kPrepareSort,
  kRadixSort,
  kRasterize,
  kMultiviewProjection,
  kTiledCount,
  kTiledScan,
  kTiledEmit,
  kTiledRadixSort,
  kTiledRanges,
  kTiledRasterize,
  kCount,
};

inline constexpr size_t kGpuPassCount = static_cast<size_t>(GpuPass::kCount);
inline constexpr u32 kGpuTimingSchemaVersion = 2U;

struct GpuFrameTimings
{
  u64 frame_id{};
  std::array<std::optional<double>, kGpuPassCount> pass_ms{};
  double frame_ms{};
};

}// namespace vkgsplat

#endif// VKGSPLAT_GPU_TIMINGS_HPP
