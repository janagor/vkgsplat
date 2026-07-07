#include "graphics_pipeline.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "vulkan_context.hpp"
#include "descriptor/descriptor_heap.hpp"
#include "initializers.hpp"
#include "shader.hpp"

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat {

auto create_graphics_pipeline(Init &init, RenderData &data) -> int
{
  auto const vert_code = read_file(std::string(SHADER_DIRECTORY) + "/triangle.vert.spv");
  auto const frag_code = read_file(std::string(SHADER_DIRECTORY) + "/triangle.frag.spv");

  VkShaderModule vert_module = create_shader_module(init, vert_code);
  VkShaderModule frag_module = create_shader_module(init, frag_code);

  if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
    std::cout << "failed to create shader module\n";
    return -1;
  }

  VkPipelineShaderStageCreateInfo vert_stage_info =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vert_module, "main");

  std::array<VkDescriptorSetAndBindingMappingEXT, 1> vertex_mappings = { VkDescriptorSetAndBindingMappingEXT{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
    .pNext = nullptr,
    .descriptorSet = 0,
    .firstBinding = 0,
    .bindingCount = 3,
    .resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
    .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
    .sourceData = { .constantOffset = { .heapOffset = heap_slot_byte_offset(data, HeapSlot::Position),
                      .heapArrayStride = static_cast<uint32_t>(data.descriptor_stride),
                      .pEmbeddedSampler = nullptr,
                      .samplerHeapOffset = 0,
                      .samplerHeapArrayStride = 0 } },
  } };

  VkShaderDescriptorSetAndBindingMappingInfoEXT vertex_mapping_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
    .pNext = nullptr,
    .mappingCount = static_cast<uint32_t>(vertex_mappings.size()),
    .pMappings = vertex_mappings.data(),
  };

  vert_stage_info.pNext = &vertex_mapping_info;

  VkPipelineShaderStageCreateInfo const frag_stage_info =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, frag_module, "main");

  std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = { vert_stage_info, frag_stage_info };

  auto const vertex_input_info = initializers::PipelineVertexInputStateCreateInfo({}, {});

  auto const input_assembly =
    initializers::PipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);

  VkViewport viewport = {};
  viewport.x = 0.0F;
  viewport.y = 0.0F;
  viewport.width = static_cast<float>(init.swapchain.extent.width);
  viewport.height = static_cast<float>(init.swapchain.extent.height);
  viewport.minDepth = 0.0F;
  viewport.maxDepth = 1.0F;

  VkRect2D scissor = {};
  scissor.offset = { .x = 0, .y = 0 };
  scissor.extent = init.swapchain.extent;

  auto const viewport_state =
    initializers::PipelineViewportStateCreateInfo(std::span{ &viewport, 1 }, std::span{ &scissor, 1 });

  auto const rasterizer = initializers::PipelineRasterizationStateCreateInfo(
    VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);

  auto const multisampling = initializers::PipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

  VkPipelineColorBlendAttachmentState color_blend_attachment = {};
  // NOLINTBEGIN(hicpp-signed-bitwise)
  color_blend_attachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  // NOLINTEND(hicpp-signed-bitwise)
  color_blend_attachment.blendEnable = VK_FALSE;

  std::array<VkPipelineColorBlendAttachmentState, 1> color_blend_attachments = { color_blend_attachment };
  auto const color_blending =
    initializers::PipelineColorBlendStateCreateInfo(color_blend_attachments, VK_FALSE, VK_LOGIC_OP_COPY);

  std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  auto dynamic_info = initializers::PipelineDynamicStateCreateInfo(dynamic_states);

  VkPipelineRenderingCreateInfo pipeline_rendering_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .pNext = nullptr,
    .viewMask = 0,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &init.swapchain.image_format,
    .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
    .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
  };

  auto pipeline_info = initializers::GraphicsPipelineCreateInfo();
  VkPipelineCreateFlags2CreateInfo pipeline_flags = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
    .pNext = &pipeline_rendering_info,
    .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
  };
  pipeline_info.pNext = &pipeline_flags;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages.data();
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_info;
  pipeline_info.layout = VK_NULL_HANDLE;
  pipeline_info.renderPass = VK_NULL_HANDLE;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

  if (init.disp.createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &data.graphics_pipeline)
      != VK_SUCCESS) {
    std::cout << "failed to create pipline\n";
    return -1;
  }

  init.disp.destroyShaderModule(frag_module, nullptr);
  init.disp.destroyShaderModule(vert_module, nullptr);
  return 0;
}

}// namespace vkgsplat
