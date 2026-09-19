#ifndef VKGSPLAT_IO_LOAD_SPLATS_HPP
#define VKGSPLAT_IO_LOAD_SPLATS_HPP

#include <expected>
#include <string_view>

#include <vkgsplat_io/splat_cpu.hpp>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

/** Load Gaussian geometry and appearance columns from a 3DGS PLY file. */
[[nodiscard]] auto LoadSplatsFromPly(std::string_view ply_path) -> std::expected<SplatCpuData, Error>;

}// namespace vkgsplat

#endif// VKGSPLAT_IO_LOAD_SPLATS_HPP
