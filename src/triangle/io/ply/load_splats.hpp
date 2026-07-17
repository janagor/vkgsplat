#pragma once

#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "types.hpp"

namespace vkgsplat {

struct SplatCpuData
{
  std::vector<std::array<f32, 3>> positions;
  std::vector<f32> gray_colors;
};

[[nodiscard]] auto load_splats_from_ply(std::string_view ply_path, u32 count) -> std::expected<SplatCpuData, std::string>;

}// namespace vkgsplat
