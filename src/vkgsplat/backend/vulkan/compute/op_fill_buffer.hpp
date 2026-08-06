#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_FILL_BUFFER_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_FILL_BUFFER_HPP

#include "compute/operation.hpp"

#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

struct FillBufferParams
{
  VkBuffer buffer{};
  VkDeviceSize offset{};
  VkDeviceSize size{};
  uint32_t value{};
};

class OpFillBuffer : public Operation
{
public:
  explicit OpFillBuffer(FillBufferParams params);

  void record(Init const &init, RenderData const &data, VkCommandBuffer command_buffer) override;

private:
  FillBufferParams params_;
};

}// namespace vkgsplat::compute

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_OP_FILL_BUFFER_HPP
