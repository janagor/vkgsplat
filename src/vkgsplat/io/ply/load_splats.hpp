#ifndef VKGSPLAT_IO_PLY_LOAD_SPLATS_HPP
#define VKGSPLAT_IO_PLY_LOAD_SPLATS_HPP

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "gs/gaussian_splat.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

namespace vkgsplat {

struct SplatCpuData
{
  std::vector<gs::GaussianGeometry> geometries;
  std::vector<gs::GaussianAppearance> appearances;
};

[[nodiscard]] auto LoadSplatsFromPly(std::string_view ply_path, u32 count) -> std::expected<SplatCpuData, Error>;

}// namespace vkgsplat

#endif// VKGSPLAT_IO_PLY_LOAD_SPLATS_HPP
