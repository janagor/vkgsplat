#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_OPERATION_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_OPERATION_HPP

#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace vulkan { struct Context; }
struct RenderData;

namespace compute {

  class Operation
  {
  public:
    Operation() = default;
    Operation(Operation const &) = delete;
    auto operator=(Operation const &) -> Operation & = delete;
    Operation(Operation &&) = delete;
    auto operator=(Operation &&) -> Operation & = delete;
    virtual ~Operation() = default;

    virtual void pre_eval([[maybe_unused]] vulkan::Context &context,
      [[maybe_unused]] RenderData const &data,
      [[maybe_unused]] VkCommandBuffer cmd)
    {}
    virtual void record([[maybe_unused]] vulkan::Context const &context,
      [[maybe_unused]] RenderData const &data,
      [[maybe_unused]] VkCommandBuffer cmd) = 0;
    virtual void post_eval([[maybe_unused]] vulkan::Context &context,
      [[maybe_unused]] RenderData const &data,
      [[maybe_unused]] VkCommandBuffer cmd)
    {}
  };

}// namespace compute

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_OPERATION_HPP
