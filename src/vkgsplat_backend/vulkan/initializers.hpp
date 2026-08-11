#ifndef VKGSPLAT_BACKEND_VULKAN_INITIALIZERS_HPP
#define VKGSPLAT_BACKEND_VULKAN_INITIALIZERS_HPP

#include <cstddef>
#include <span>

#include <vulkan/vulkan_core.h>

#include <vkgsplat_utility/types.hpp>

namespace vkgsplat::initializers {

inline auto RenderPassCreateInfo(std::span<VkAttachmentDescription const> attachments,
  std::span<VkSubpassDescription const> subpasses,
  std::span<VkSubpassDependency const> dependencies,
  VkRenderPassCreateFlags flags = 0) -> VkRenderPassCreateInfo
{
  return VkRenderPassCreateInfo{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .attachmentCount = static_cast<u32>(attachments.size()),
    .pAttachments = attachments.data(),
    .subpassCount = static_cast<u32>(subpasses.size()),
    .pSubpasses = subpasses.data(),
    .dependencyCount = static_cast<u32>(dependencies.size()),
    .pDependencies = dependencies.data(),
  };
}

inline auto ShaderModuleCreateInfo(std::span<u32 const> code) -> VkShaderModuleCreateInfo
{
  return VkShaderModuleCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .codeSize = static_cast<std::size_t>(code.size_bytes()),
    .pCode = code.data(),
  };
}

inline auto DescriptorSetLayoutCreateInfo(std::span<VkDescriptorSetLayoutBinding const> bindings,
  VkDescriptorSetLayoutCreateFlags flags = 0) -> VkDescriptorSetLayoutCreateInfo
{
  return VkDescriptorSetLayoutCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .bindingCount = static_cast<u32>(bindings.size()),
    .pBindings = bindings.data(),
  };
}

inline auto PipelineShaderStageCreateInfo(VkShaderStageFlagBits stage,
  VkShaderModule module,
  char const *p_name,
  VkSpecializationInfo const *p_specialization_info = nullptr,
  VkPipelineShaderStageCreateFlags flags = 0) -> VkPipelineShaderStageCreateInfo
{
  return VkPipelineShaderStageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .stage = stage,
    .module = module,
    .pName = p_name,
    .pSpecializationInfo = p_specialization_info,
  };
}

inline auto PipelineVertexInputStateCreateInfo(std::span<VkVertexInputBindingDescription const> binding_descriptions,
  std::span<VkVertexInputAttributeDescription const> attribute_descriptions) -> VkPipelineVertexInputStateCreateInfo
{
  return VkPipelineVertexInputStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .vertexBindingDescriptionCount = static_cast<u32>(binding_descriptions.size()),
    .pVertexBindingDescriptions = binding_descriptions.data(),
    .vertexAttributeDescriptionCount = static_cast<u32>(attribute_descriptions.size()),
    .pVertexAttributeDescriptions = attribute_descriptions.data(),
  };
}

inline auto PipelineInputAssemblyStateCreateInfo(VkPrimitiveTopology topology, VkBool32 primitive_restart_enable)
  -> VkPipelineInputAssemblyStateCreateInfo
{
  return VkPipelineInputAssemblyStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .topology = topology,
    .primitiveRestartEnable = primitive_restart_enable,
  };
}

inline auto PipelineViewportStateCreateInfo(u32 viewport_count, u32 scissor_count) -> VkPipelineViewportStateCreateInfo
{
  return VkPipelineViewportStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .viewportCount = viewport_count,
    .pViewports = nullptr,
    .scissorCount = scissor_count,
    .pScissors = nullptr,
  };
}

inline auto PipelineViewportStateCreateInfo(std::span<VkViewport const> viewports, std::span<VkRect2D const> scissors)
  -> VkPipelineViewportStateCreateInfo
{
  return VkPipelineViewportStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .viewportCount = static_cast<u32>(viewports.size()),
    .pViewports = viewports.data(),
    .scissorCount = static_cast<u32>(scissors.size()),
    .pScissors = scissors.data(),
  };
}

inline auto PipelineRasterizationStateCreateInfo(VkPolygonMode polygon_mode,
  VkCullModeFlags cull_mode,
  VkFrontFace front_face) -> VkPipelineRasterizationStateCreateInfo
{
  return VkPipelineRasterizationStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = polygon_mode,
    .cullMode = cull_mode,
    .frontFace = front_face,
    .depthBiasEnable = VK_FALSE,
    .depthBiasConstantFactor = 0.0F,
    .depthBiasClamp = 0.0F,
    .depthBiasSlopeFactor = 0.0F,
    .lineWidth = 1.0F,
  };
}

inline auto PipelineMultisampleStateCreateInfo(VkSampleCountFlagBits rasterization_samples)
  -> VkPipelineMultisampleStateCreateInfo
{
  return VkPipelineMultisampleStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .rasterizationSamples = rasterization_samples,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 0.0F,
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = 0U,
    .alphaToOneEnable = 0U,
  };
}

inline auto PipelineDepthStencilStateCreateInfo(VkBool32 depth_test_enable,
  VkBool32 depth_write_enable,
  VkCompareOp depth_compare_op,
  VkPipelineDepthStencilStateCreateFlags flags = 0) -> VkPipelineDepthStencilStateCreateInfo
{
  return VkPipelineDepthStencilStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .depthTestEnable = depth_test_enable,
    .depthWriteEnable = depth_write_enable,
    .depthCompareOp = depth_compare_op,
    .depthBoundsTestEnable = VK_FALSE,
    .stencilTestEnable = VK_FALSE,
    .front = {},
    .back = {},
    .minDepthBounds = 0.0F,
    .maxDepthBounds = 0.0F,
  };
}

inline auto PipelineColorBlendStateCreateInfo(std::span<VkPipelineColorBlendAttachmentState const> states,
  VkBool32 logic_op_enable,
  VkLogicOp logic_op,
  VkPipelineColorBlendStateCreateFlags flags = 0) -> VkPipelineColorBlendStateCreateInfo
{
  return VkPipelineColorBlendStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .logicOpEnable = logic_op_enable,
    .logicOp = logic_op,
    .attachmentCount = static_cast<u32>(states.size()),
    .pAttachments = states.data(),
    .blendConstants = { 0.0F, 0.0F, 0.0F, 0.0F },
  };
}

inline auto PipelineDynamicStateCreateInfo(std::span<VkDynamicState const> states) -> VkPipelineDynamicStateCreateInfo
{
  return VkPipelineDynamicStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .dynamicStateCount = static_cast<u32>(states.size()),
    .pDynamicStates = states.data(),
  };
}

inline auto PipelineLayoutCreateInfo(std::span<VkDescriptorSetLayout const> set_layouts,
  std::span<VkPushConstantRange const> push_constant_ranges,
  VkPipelineLayoutCreateFlags flags = 0) -> VkPipelineLayoutCreateInfo
{
  return VkPipelineLayoutCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext = VK_NULL_HANDLE,
    .flags = flags,
    .setLayoutCount = static_cast<u32>(set_layouts.size()),
    .pSetLayouts = set_layouts.data(),
    .pushConstantRangeCount = static_cast<u32>(push_constant_ranges.size()),
    .pPushConstantRanges = push_constant_ranges.data(),
  };
}

inline auto GraphicsPipelineCreateInfo() -> VkGraphicsPipelineCreateInfo
{
  VkGraphicsPipelineCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.pNext = nullptr;
  return info;
}

inline auto FramebufferCreateInfo(VkRenderPass render_pass,
  std::span<VkImageView const> attachments,
  VkExtent2D const &extent2d,
  u32 layers,
  VkFramebufferCreateFlags flags = 0) -> VkFramebufferCreateInfo
{
  return VkFramebufferCreateInfo{
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .renderPass = render_pass,
    .attachmentCount = static_cast<u32>(attachments.size()),
    .pAttachments = attachments.data(),
    .width = extent2d.width,
    .height = extent2d.height,
    .layers = layers,
  };
}

inline auto CommandPoolCreateInfo(u32 queue_family_index, VkCommandPoolCreateFlags flags = 0) -> VkCommandPoolCreateInfo
{
  return VkCommandPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .queueFamilyIndex = queue_family_index,
  };
}

inline auto MemoryAllocateInfo(VkDeviceSize allocation_size, u32 memory_type_index) -> VkMemoryAllocateInfo
{
  return VkMemoryAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext = nullptr,
    .allocationSize = allocation_size,
    .memoryTypeIndex = memory_type_index,
  };
}

inline auto WriteDescriptorSet(VkDescriptorSet dst_set,
  u32 dst_binding,
  u32 dst_array_element,
  u32 descriptor_count,
  VkDescriptorType descriptor_type,
  VkDescriptorImageInfo const *p_image_info,
  VkDescriptorBufferInfo const *p_buffer_info,
  VkBufferView const *p_texel_buffer_view) -> VkWriteDescriptorSet
{
  return VkWriteDescriptorSet{
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .pNext = nullptr,
    .dstSet = dst_set,
    .dstBinding = dst_binding,
    .dstArrayElement = dst_array_element,
    .descriptorCount = descriptor_count,
    .descriptorType = descriptor_type,
    .pImageInfo = p_image_info,
    .pBufferInfo = p_buffer_info,
    .pTexelBufferView = p_texel_buffer_view,

  };
}

inline auto WriteDescriptorSet(VkDescriptorSet dst_set,
  u32 dst_binding,
  u32 dst_array_element,
  VkDescriptorType descriptor_type,
  std::span<VkDescriptorImageInfo const> descriptor_image_infos) -> VkWriteDescriptorSet
{
  return WriteDescriptorSet(dst_set,
    dst_binding,
    dst_array_element,
    static_cast<u32>(descriptor_image_infos.size()),
    descriptor_type,
    descriptor_image_infos.data(),
    nullptr,
    nullptr);
}

inline auto WriteDescriptorSet(VkDescriptorSet dst_set,
  u32 dst_binding,
  u32 dst_array_element,
  VkDescriptorType descriptor_type,
  std::span<VkDescriptorBufferInfo const> descriptor_buffer_infos) -> VkWriteDescriptorSet
{
  return WriteDescriptorSet(dst_set,
    dst_binding,
    dst_array_element,
    static_cast<u32>(descriptor_buffer_infos.size()),
    descriptor_type,
    nullptr,
    descriptor_buffer_infos.data(),
    nullptr);
}

inline auto WriteDescriptorSet(VkDescriptorSet dst_set,
  u32 dst_binding,
  u32 dst_array_element,
  VkDescriptorType descriptor_type,
  std::span<VkBufferView const> texel_buffer_views) -> VkWriteDescriptorSet
{
  return WriteDescriptorSet(dst_set,
    dst_binding,
    dst_array_element,
    static_cast<u32>(texel_buffer_views.size()),
    descriptor_type,
    nullptr,
    nullptr,
    texel_buffer_views.data());
}

inline auto RenderPassBeginInfo(VkRenderPass render_pass,
  VkFramebuffer framebuffer,
  VkRect2D const &render_area,
  std::span<VkClearValue const> clear_values) -> VkRenderPassBeginInfo
{
  return VkRenderPassBeginInfo{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .pNext = nullptr,
    .renderPass = render_pass,
    .framebuffer = framebuffer,
    .renderArea = render_area,
    .clearValueCount = static_cast<u32>(clear_values.size()),
    .pClearValues = clear_values.data(),
  };
}

inline auto BufferCreateInfo(VkDeviceSize size, VkBufferUsageFlags usage, VkBufferCreateFlags flags = 0)
  -> VkBufferCreateInfo
{
  return VkBufferCreateInfo{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .size = size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = nullptr,
  };
}

inline auto ImageCreateInfo() -> VkImageCreateInfo
{
  return VkImageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = VK_FORMAT_UNDEFINED,
    .extent = { .width = 0, .height = 0, .depth = 0 },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = 0,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = nullptr,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
}

inline auto ImageMemoryBarrier(VkImageLayout old_layout,
  VkImageLayout new_layout,
  VkImage image,
  VkImageSubresourceRange subresource_range) -> VkImageMemoryBarrier
{
  return VkImageMemoryBarrier{
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_NONE,
    .dstAccessMask = VK_ACCESS_NONE,
    .oldLayout = old_layout,
    .newLayout = new_layout,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = image,
    .subresourceRange = subresource_range,
  };
}

inline auto ImageViewCreateInfo(VkImage image,
  VkImageViewType view_type,
  VkFormat format,
  VkImageSubresourceRange subresource_range,
  VkImageViewCreateFlags flags = 0,
  VkComponentMapping components = VkComponentMapping{
    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
  }) -> VkImageViewCreateInfo
{
  return VkImageViewCreateInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
    .image = image,
    .viewType = view_type,
    .format = format,
    .components = components,
    .subresourceRange = subresource_range,
  };
}

inline auto SamplerCreateInfo() -> VkSamplerCreateInfo
{
  VkSamplerCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.pNext = VK_NULL_HANDLE;
  return info;
}

inline auto DescriptorPoolCreateInfo(std::span<VkDescriptorPoolSize const> sizes, u32 max_sets)
  -> VkDescriptorPoolCreateInfo
{
  VkDescriptorPoolCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  info.poolSizeCount = static_cast<u32>(sizes.size());
  info.pPoolSizes = sizes.data();
  info.maxSets = max_sets;
  return info;
}

inline auto DescriptorSetAllocateInfo(VkDescriptorPool descriptor_pool,
  std::span<VkDescriptorSetLayout const> descriptor_set_layouts) -> VkDescriptorSetAllocateInfo
{
  return VkDescriptorSetAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext = nullptr,
    .descriptorPool = descriptor_pool,
    .descriptorSetCount = static_cast<u32>(descriptor_set_layouts.size()),
    .pSetLayouts = descriptor_set_layouts.data(),
  };
}

inline auto CommandBufferAllocateInfo(VkCommandPool command_pool, VkCommandBufferLevel level, u32 command_buffer_count)
  -> VkCommandBufferAllocateInfo
{
  return VkCommandBufferAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext = nullptr,
    .commandPool = command_pool,
    .level = level,
    .commandBufferCount = command_buffer_count,
  };
}

inline auto SemaphoreCreateInfo() -> VkSemaphoreCreateInfo
{
  return VkSemaphoreCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
  };
}
inline auto FenceCreateInfo(VkFenceCreateFlags flags = 0) -> VkFenceCreateInfo
{
  return VkFenceCreateInfo{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .pNext = nullptr,
    .flags = flags,
  };
}

inline auto CommandBufferBeginInfo(VkCommandBufferUsageFlags flags = 0) -> VkCommandBufferBeginInfo
{
  return VkCommandBufferBeginInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = nullptr,
    .flags = flags,
    .pInheritanceInfo = nullptr,
  };
}

inline auto SubmitInfo(std::span<VkSemaphore const> wait_semaphores,
  std::span<VkPipelineStageFlags const> wait_dst_stage_mask,
  std::span<VkCommandBuffer const> command_buffers,
  std::span<VkSemaphore const> signal_semaphores) -> VkSubmitInfo
{
  return VkSubmitInfo{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext = nullptr,
    .waitSemaphoreCount = static_cast<u32>(wait_semaphores.size()),
    .pWaitSemaphores = wait_semaphores.data(),
    .pWaitDstStageMask = wait_dst_stage_mask.data(),
    .commandBufferCount = static_cast<u32>(command_buffers.size()),
    .pCommandBuffers = command_buffers.data(),
    .signalSemaphoreCount = static_cast<u32>(signal_semaphores.size()),
    .pSignalSemaphores = signal_semaphores.data(),
  };
}

inline auto PresentInfoKHR(std::span<VkSemaphore const> wait_semaphores,
  std::span<VkSwapchainKHR const> swapchains,
  std::span<u32 const> indices) -> VkPresentInfoKHR
{
  return VkPresentInfoKHR{
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pNext = nullptr,
    .waitSemaphoreCount = static_cast<u32>(wait_semaphores.size()),
    .pWaitSemaphores = wait_semaphores.data(),
    .swapchainCount = static_cast<u32>(swapchains.size()),
    .pSwapchains = swapchains.data(),
    .pImageIndices = indices.data(),
    .pResults = nullptr,
  };
}

}// namespace vkgsplat::initializers

#endif// VKGSPLAT_BACKEND_VULKAN_INITIALIZERS_HPP
