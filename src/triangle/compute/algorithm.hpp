#pragma once
#include "vulkan_context.hpp"
#include <span>
#include <string>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

struct Init;
struct RenderData;

namespace compute {

  class ParamList;

  struct DescriptorMapping
  {
    uint32_t binding;
    uint32_t heap_offset;
  };

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
      RenderData const &data,
      std::string const &shader_path,
      std::span<DescriptorMapping const> mappings,
      std::span<const uint32_t> specialization_constants = {}) -> bool;

    [[nodiscard]] auto init(Init &init,
      RenderData const &data,
      std::string const &shader_path,
      ParamList const &params,
      std::span<const uint32_t> specialization_constants = {}) -> bool;

    void destroy(Init &init);

    [[nodiscard]] auto pipeline() const -> VkPipeline { return pipeline_; }

  private:
    VkPipeline pipeline_{ VK_NULL_HANDLE };
  };

}// namespace compute

}// namespace vkgsplat
