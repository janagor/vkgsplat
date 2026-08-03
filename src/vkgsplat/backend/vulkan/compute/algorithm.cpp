#include "compute/algorithm.hpp"
#include "backend/vulkan/initializers.hpp"
#include "shader.hpp"
#include "vulkan_context.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

Algorithm::Algorithm(Algorithm &&other) noexcept : pipeline_(std::exchange(other.pipeline_, VK_NULL_HANDLE)) {}

auto Algorithm::operator=(Algorithm &&other) noexcept -> Algorithm &
{
  if (this != &other) { pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE); }
  return *this;
}

auto Algorithm::init(Init &init, std::string const &shader_path, std::span<const uint32_t> specialization_constants)
  -> bool
{
  auto const comp_code = ReadFile(shader_path);
  VkShaderModule comp_module = CreateShaderModule(init, comp_code);
  if (comp_module == VK_NULL_HANDLE) {
    std::println("Failed to create compute shader module: {}", shader_path);
    return false;
  }

  VkPipelineShaderStageCreateInfo stage =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, comp_module, "main");

  VkSpecializationInfo specialization_info{};
  constexpr size_t kMaxSpecializationConstants = 8;
  std::array<VkSpecializationMapEntry, kMaxSpecializationConstants> specialization_map{};
  if (!specialization_constants.empty()) {
    auto const count = std::min(specialization_constants.size(), specialization_map.size());
    std::ranges::for_each(std::views::iota(size_t{ 0 }, count), [&](size_t index) -> void {
      specialization_map.at(index) = VkSpecializationMapEntry{
        .constantID = static_cast<uint32_t>(index),
        .offset = static_cast<uint32_t>(index * sizeof(uint32_t)),
        .size = sizeof(uint32_t),
      };
    });
    specialization_info.mapEntryCount = static_cast<uint32_t>(specialization_constants.size());
    specialization_info.pMapEntries = specialization_map.data();
    specialization_info.dataSize = specialization_constants.size_bytes();
    specialization_info.pData = specialization_constants.data();
    stage.pSpecializationInfo = &specialization_info;
  }

  VkPipelineCreateFlags2CreateInfo pipeline_flags = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
  };

  VkComputePipelineCreateInfo const pipeline_info = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = &pipeline_flags,
    .flags = 0,
    .stage = stage,
    .layout = VK_NULL_HANDLE,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = -1,
  };

  if (init.disp.createComputePipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_) != VK_SUCCESS) {
    std::println("Failed to create algorithm compute pipeline!");
    init.disp.destroyShaderModule(comp_module, nullptr);
    return false;
  }

  init.disp.destroyShaderModule(comp_module, nullptr);
  return true;
}

void Algorithm::destroy(Init &init) noexcept
{
  if (pipeline_ != VK_NULL_HANDLE) { init.disp.destroyPipeline(std::exchange(pipeline_, VK_NULL_HANDLE), nullptr); }
}

}// namespace vkgsplat::compute
