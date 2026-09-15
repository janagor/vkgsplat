#ifndef VKGSPLAT_BACKEND_VULKAN_COMPUTE_ALGORITHM_HPP
#define VKGSPLAT_BACKEND_VULKAN_COMPUTE_ALGORITHM_HPP

#include "vulkan_context.hpp"

#include <vkexec/pipeline.hpp>
#include <vkexec_extensions/descriptor_heap/heap_compute_pipeline.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace vkgsplat {

namespace vulkan {
  struct Context;
}
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

    [[nodiscard]] auto init(vulkan::Context &context,
      std::string const &shader_path,
      std::span<const uint32_t> specialization_constants = {},
      std::array<uint32_t, 3> local_size = vkexec::k_default_local_size) -> bool;

    void destroy(vulkan::Context &context) noexcept;

    [[nodiscard]] auto pipeline() const noexcept -> VkPipeline;

  private:
    std::optional<vkexec::heap_compute_pipeline> pipeline_;
  };

}// namespace compute

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_COMPUTE_ALGORITHM_HPP
