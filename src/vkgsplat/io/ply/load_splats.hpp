#pragma once

#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <vkgsplat/types.hpp>

namespace vkgsplat {

struct SplatCpuData
{
  std::vector<std::array<f32, 3>> positions;
  std::vector<std::array<f32, 3>> colors;
};

[[nodiscard]] auto load_splats_from_ply(std::string_view ply_path, u32 count) -> std::expected<SplatCpuData, std::string>;

}// namespace vkgsplat
