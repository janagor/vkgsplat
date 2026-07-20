#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "gaussian_splat.hpp"
#include <vkgsplat/types.hpp>

namespace vkgsplat {

struct SplatCpuData
{
  std::vector<GaussianGeometry> geometries;
  std::vector<GaussianAppearance> appearances;
};

[[nodiscard]] auto load_splats_from_ply(std::string_view ply_path, u32 count) -> std::expected<SplatCpuData, std::string>;

}// namespace vkgsplat
