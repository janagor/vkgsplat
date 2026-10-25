#ifndef VKGSPLAT_BACKEND_VULKAN_GS_TILED_COMPUTE_OPERATIONS_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_TILED_COMPUTE_OPERATIONS_HPP

#include <expected>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {
struct RenderData;
namespace vulkan {
  struct Context;
}
namespace gs::tiled {

  [[nodiscard]] auto InitAlgorithms(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>;
  void DestroyAlgorithms(RenderData &data);
  [[nodiscard]] auto RecordPreparation(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>;
  [[nodiscard]] auto RecordRasterization(vulkan::Context const &context,
    RenderData const &data,
    VkCommandBuffer command_buffer) -> std::expected<void, Error>;
  [[nodiscard]] auto ReadFrameStatus(RenderData const &data) -> std::expected<void, Error>;
  [[nodiscard]] auto DecodeFrameStatus(u32 status) -> std::expected<void, Error>;

}// namespace gs::tiled
}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GS_TILED_COMPUTE_OPERATIONS_HPP
