#pragma once
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

struct Init;
struct RenderData;

namespace compute {

  class Operation
  {
  public:
    virtual ~Operation() = default;

    virtual void pre_eval([[maybe_unused]] Init &init,
      [[maybe_unused]] RenderData const &data,
      [[maybe_unused]] VkCommandBuffer cmd)
    {}
    virtual void record([[maybe_unused]] Init const &init,
      [[maybe_unused]] RenderData const &data,
      [[maybe_unused]] VkCommandBuffer cmd) = 0;
    virtual void post_eval([[maybe_unused]] Init &init,
      [[maybe_unused]] RenderData const &data,
      [[maybe_unused]] VkCommandBuffer cmd)
    {}
  };

}// namespace compute

}// namespace vkgsplat
