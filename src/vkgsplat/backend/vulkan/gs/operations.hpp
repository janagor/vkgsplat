#ifndef VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP

#include "compute/operation.hpp"

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace vulkan { struct Context; }
struct RenderData;

namespace gs {

// Projection dispatch; `time_pass` writes GpuPass::kProjection timestamps (once per frame only).
void RecordProjection(vulkan::Context const &context,
  RenderData const &data,
  VkCommandBuffer command_buffer,
  bool time_pass);

class OpProjection : public compute::Operation
{
public:
  void record(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer) override;
};

class OpBinning : public compute::Operation
{
public:
  void record(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer) override;
};

class OpPrepareSort : public compute::Operation
{
public:
  void record(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer) override;
};

class OpRadixSort : public compute::Operation
{
public:
  void record(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer) override;
};

class OpRasterization : public compute::Operation
{
public:
  void record(vulkan::Context const &context, RenderData const &data, VkCommandBuffer command_buffer) override;
};

}// namespace gs

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_GS_OPERATIONS_HPP
