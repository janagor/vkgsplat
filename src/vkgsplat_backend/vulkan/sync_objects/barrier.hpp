#ifndef VKGSPLAT_BACKEND_VULKAN_SYNC_OBJECTS_BARRIER_HPP
#define VKGSPLAT_BACKEND_VULKAN_SYNC_OBJECTS_BARRIER_HPP

#include <VkBootstrapDispatch.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

struct MemoryBarrierParams
{
  VkPipelineStageFlags src_stage{};
  VkPipelineStageFlags dst_stage{};
  VkAccessFlags src_access{};
  VkAccessFlags dst_access{};
};

class Barrier
{
public:
  Barrier() = delete;

  static void memory(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer, MemoryBarrierParams params);

  static void transfer_to_compute(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer);

  static void compute_to_compute(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer);

  // After compute writes buffers consumed by DrawIndirect + vertex/fragment shaders.
  static void compute_to_graphics(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer);

  // After graphics reads projected attrs before the next compute projection rewrite.
  static void graphics_to_compute(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer);

  static void compute_read(vkb::DispatchTable const &disp, VkCommandBuffer command_buffer);
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_SYNC_OBJECTS_BARRIER_HPP
