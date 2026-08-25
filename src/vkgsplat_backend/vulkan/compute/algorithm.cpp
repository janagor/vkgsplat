#include "compute/algorithm.hpp"

#include "shader.hpp"
#include "vulkan_context.hpp"

#include <vkexec/compute_pipeline.hpp>
#include <vkexec/pipeline.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <cstdint>
#include <exception>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <vector>

namespace vkgsplat::compute {

Algorithm::Algorithm(Algorithm &&other) noexcept : pipeline_(other.pipeline_) { other.pipeline_.reset(); }

auto Algorithm::operator=(Algorithm &&other) noexcept -> Algorithm &
{
  if (this != &other) {
    pipeline_ = other.pipeline_;
    other.pipeline_.reset();
  }
  return *this;
}

auto Algorithm::init(vulkan::Context &context,
  std::string const &shader_path,
  std::span<const uint32_t> specialization_constants,
  std::array<uint32_t, 3> local_size) -> bool
{
  if (context.vkexec_context == nullptr) {
    std::println("vkexec context missing for compute pipeline: {}", shader_path);
    return false;
  }

  auto const comp_code = ReadFile(shader_path);
  if (comp_code.size() < sizeof(uint32_t) || (comp_code.size() % sizeof(uint32_t)) != 0) {
    std::println("Invalid SPIR-V size for compute shader: {}", shader_path);
    return false;
  }

  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  auto const spirv = std::span{ reinterpret_cast<uint32_t const *>(comp_code.data()),
    comp_code.size() / sizeof(uint32_t) };
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  vkexec::layout_desc desc{};
  desc.descriptor_heap = true;
  desc.push_constant_size = 0;
  desc.local_size = local_size;
  desc.specialization.assign(specialization_constants.begin(), specialization_constants.end());

  try {
    pipeline_ = vkexec::compute_pipeline::from_spirv(*context.vkexec_context, spirv, desc);
  } catch (std::exception const &ex) {
    std::println("Failed to create algorithm compute pipeline ({}): {}", shader_path, ex.what());
    pipeline_.reset();
    return false;
  }
  return true;
}

void Algorithm::destroy(vulkan::Context &context) noexcept
{
  (void)context;
  // Pipeline lifetime is owned by the vkexec context pipeline cache.
  pipeline_.reset();
}

auto Algorithm::pipeline() const noexcept -> VkPipeline
{
  if (!pipeline_) { return VK_NULL_HANDLE; }
  return pipeline_->resources().pipeline;
}

}// namespace vkgsplat::compute
