#ifndef VKGSPLAT_BACKEND_VULKAN_GS_LOAD_HEAP_PIPELINE_HPP
#define VKGSPLAT_BACKEND_VULKAN_GS_LOAD_HEAP_PIPELINE_HPP

#include "shader.hpp"
#include "vulkan_context.hpp"

#include <vkexec/pipeline.hpp>
#include <vkexec/sync_wait.hpp>
#include <vkexec_extensions/descriptor_heap/algorithm.hpp>
#include <vkexec_extensions/descriptor_heap/strategy.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <vector>

namespace vkgsplat::gs {

[[nodiscard]] inline auto LoadHeapAlgorithm(vulkan::Context &context,
  std::string const &shader_path,
  std::span<const uint32_t> specialization_constants = {},
  std::array<uint32_t, 3> local_size = vkexec::k_default_local_size) -> std::optional<vkexec::algorithm>
{
  if (context.vkexec_context == nullptr) {
    std::println("vkexec context missing for compute pipeline: {}", shader_path);
    return std::nullopt;
  }

  auto const comp_code = ReadFile(shader_path);
  if (comp_code.size() < sizeof(uint32_t) || (comp_code.size() % sizeof(uint32_t)) != 0) {
    std::println("Invalid SPIR-V size for compute shader: {}", shader_path);
    return std::nullopt;
  }

  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  auto const spirv = std::span{ reinterpret_cast<uint32_t const *>(comp_code.data()),
    comp_code.size() / sizeof(uint32_t) };
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  vkexec::heap_layout_desc desc{};
  desc.local_size = local_size;
  desc.specialization.assign(specialization_constants.begin(), specialization_constants.end());

  auto created = vkexec::try_sync_wait_value(
    vkexec::algorithm::create(vkexec::descriptor_heap, *context.vkexec_context, spirv, desc));
  if (!created) {
    std::println("Failed to create heap algorithm ({}): {}", shader_path, created.error().message());
    return std::nullopt;
  }
  return std::move(*created);
}

}// namespace vkgsplat::gs

#endif// VKGSPLAT_BACKEND_VULKAN_GS_LOAD_HEAP_PIPELINE_HPP
