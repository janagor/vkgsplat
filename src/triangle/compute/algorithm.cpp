#include "app_state.hpp"
#include "compute/algorithm.hpp"
#include "initializers.hpp"
#include "shader.hpp"
#include "types.hpp"
#include "vulkan_context.hpp"
#include <print>
#include <span>
#include <string>
#include <ranges>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>//NOLINT
#include <vulkan/vulkan_core.h>

namespace vkgsplat::compute {

Algorithm::Algorithm(Algorithm &&other) noexcept : pipeline_(std::exchange(other.pipeline_, VK_NULL_HANDLE)) {}

Algorithm &Algorithm::operator=(Algorithm &&other) noexcept
{
  if (this != &other) { pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE); }
  return *this;
}

auto Algorithm::init(Init &init,
  RenderData const &data,
  std::string const &shader_path,
  std::span<DescriptorMapping const> mappings) -> bool
{

  auto const comp_code = read_file(shader_path);
  VkShaderModule comp_module = create_shader_module(init, comp_code);
  if (comp_module == VK_NULL_HANDLE) {
    std::println("Failed to create compute shader module: {}", shader_path);
    return false;
  }

  std::vector<VkDescriptorSetAndBindingMappingEXT> vk_mappings =
    mappings | std::views::transform([&data](auto const &mapping) {
      return VkDescriptorSetAndBindingMappingEXT{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
        .pNext = nullptr,
        .descriptorSet = 0,
        .firstBinding = mapping.binding,
        .bindingCount = 1,
        .resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT
                        | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
        .sourceData = { .constantOffset = { .heapOffset = mapping.heap_offset,
                          .heapArrayStride = static_cast<u32>(data.descriptor_stride),
                          .pEmbeddedSampler = nullptr,
                          .samplerHeapOffset = 0,
                          .samplerHeapArrayStride = 0 } },
      };
    })
    | std::ranges::to<std::vector>();

  VkShaderDescriptorSetAndBindingMappingInfoEXT mapping_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
    .pNext = nullptr,
    .mappingCount = static_cast<u32>(vk_mappings.size()),
    .pMappings = vk_mappings.data(),
  };

  VkPipelineShaderStageCreateInfo stage =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, comp_module, "main");
  stage.pNext = &mapping_info;

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

void Algorithm::destroy(Init &init)
{
  if (pipeline_ != VK_NULL_HANDLE) {
    init.disp.destroyPipeline(pipeline_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
  }
}

}// namespace vkgsplat::compute
