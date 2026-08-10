#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_ALGORITHM_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_ALGORITHM_HPP

#include "vulkan_context.hpp"
#include <span>
#include <string>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

namespace vulkan { struct Context; }
struct RenderData;

namespace compute {

  class Algorithm
  {
  public:
    Algorithm() noexcept = default;
    ~Algorithm() = default;

    Algorithm(Algorithm const &) = delete;
    auto operator=(Algorithm const &) -> Algorithm & = delete;
    Algorithm(Algorithm &&other) noexcept;
    auto operator=(Algorithm &&other) noexcept -> Algorithm &;

    [[nodiscard]] auto
      init(vulkan::Context &context, std::string const &shader_path, std::span<const uint32_t> specialization_constants = {}) -> bool;

    void destroy(vulkan::Context &context) noexcept;

    [[nodiscard]] auto pipeline() const noexcept -> VkPipeline { return pipeline_; }

  private:
    VkPipeline pipeline_{ VK_NULL_HANDLE };
  };

}// namespace compute

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_ALGORITHM_HPP
