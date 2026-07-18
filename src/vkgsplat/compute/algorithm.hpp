#pragma once
#include "vulkan_context.hpp"
#include <span>
#include <string>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

struct Init;
struct RenderData;

namespace compute {

  class Algorithm
  {
  public:
    Algorithm() = default;
    ~Algorithm() = default;

    Algorithm(const Algorithm &) = delete;
    Algorithm &operator=(const Algorithm &) = delete;
    Algorithm(Algorithm &&other) noexcept;
    Algorithm &operator=(Algorithm &&other) noexcept;

    [[nodiscard]] auto init(Init &init,
      std::string const &shader_path,
      std::span<const uint32_t> specialization_constants = {}) -> bool;

    void destroy(Init &init);

    [[nodiscard]] auto pipeline() const -> VkPipeline { return pipeline_; }

  private:
    VkPipeline pipeline_{ VK_NULL_HANDLE };
  };

}// namespace compute

}// namespace vkgsplat
