#ifndef VKGSPLAT_BACKEND_VULKAN_GS_TILED_COMPUTE_RESOURCES_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_TILED_COMPUTE_RESOURCES_HPP

#include <expected>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {
struct RenderData;
namespace vulkan { struct Context; }
namespace gs::tiled {

[[nodiscard]] auto InitResources(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>;
[[nodiscard]] auto RecreateExtentResources(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>;
void DestroyResources(RenderData &data);

}// namespace gs::tiled
}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GS_TILED_COMPUTE_RESOURCES_HPP
