#include "backend/vulkan/graphics_pipeline.hpp"

#include <array>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "backend/vulkan/initializers.hpp"
#include "shader.hpp"
#include "vulkan_context.hpp"

#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat {

void destroy_graphics_pipeline(Init &init, RenderData &data)
{
  if (data.graphics_pipeline != VK_NULL_HANDLE) {
    init.disp.destroyPipeline(data.graphics_pipeline, nullptr);
    data.graphics_pipeline = VK_NULL_HANDLE;
  }
}

auto create_graphics_pipeline(Init &init, RenderData &data) -> int
{
  destroy_graphics_pipeline(init, data);

  auto const vert_code = read_file(std::string(SHADER_DIRECTORY) + "/sphere.vert.spv");
  auto const frag_code = read_file(std::string(SHADER_DIRECTORY) + "/sphere.frag.spv");

  VkShaderModule vert_module = create_shader_module(init, vert_code);
  VkShaderModule frag_module = create_shader_module(init, frag_code);

  if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
    std::cout << "failed to create shader module\n";
    return -1;
  }

  VkPipelineShaderStageCreateInfo const vert_stage_info =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vert_module, "main");

  VkPipelineShaderStageCreateInfo const frag_stage_info =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, frag_module, "main");

  std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = { vert_stage_info, frag_stage_info };

  auto const vertex_input_info = initializers::PipelineVertexInputStateCreateInfo({}, {});

  auto const input_assembly =
    initializers::PipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);

  VkViewport viewport = {};
  viewport.x = 0.0F;
  viewport.y = 0.0F;
  viewport.width = static_cast<float>(init.swapchain->extent().width);
  viewport.height = static_cast<float>(init.swapchain->extent().height);
  viewport.minDepth = 0.0F;
  viewport.maxDepth = 1.0F;

  VkRect2D scissor = {};
  scissor.offset = { .x = 0, .y = 0 };
  scissor.extent = init.swapchain->vk_extent();

  auto const viewport_state =
    initializers::PipelineViewportStateCreateInfo(std::span{ &viewport, 1 }, std::span{ &scissor, 1 });

  auto const rasterizer = initializers::PipelineRasterizationStateCreateInfo(
    VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

  auto const multisampling = initializers::PipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

  auto const depth_stencil =
    initializers::PipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);

  VkPipelineColorBlendAttachmentState color_blend_attachment = {};
  // NOLINTBEGIN(hicpp-signed-bitwise)
  color_blend_attachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  // NOLINTEND(hicpp-signed-bitwise)
  // Premultiplied over (SuperSplat / PlayCanvas): src already has rgb*alpha.
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

  std::array<VkPipelineColorBlendAttachmentState, 1> color_blend_attachments = { color_blend_attachment };
  auto const color_blending =
    initializers::PipelineColorBlendStateCreateInfo(color_blend_attachments, VK_FALSE, VK_LOGIC_OP_COPY);

  std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  auto dynamic_info = initializers::PipelineDynamicStateCreateInfo(dynamic_states);

  VkFormat const color_format = data.color_format;
  VkPipelineRenderingCreateInfo pipeline_rendering_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .pNext = nullptr,
    .viewMask = 0,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &color_format,
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
  pipeline_info.pDepthStencilState = &depth_stencil;
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
