// based on https://github.com/charles-lunarg/vk-bootstrap/blob/main/example/triangle.cpp
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "error.hpp"
#include "initializers.hpp"
#include "types.hpp"

#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

#include "vkgsplat/example_config.h"

#include <backend/vulkan/gpu_allocator.hpp>

const int MAX_FRAMES_IN_FLIGHT = 2;


static constexpr std::array<std::array<float, 2>, 3> positions = { {
  { 0.0, -0.5 },
  { 0.5, 0.5 },
  { -0.5, 0.5 },
} };

static constexpr std::array<std::array<float, 3>, 3> colors = { {
  { 1.0, 0.0, 0.0 },
  { 0.0, 1.0, 0.0 },
  { 0.0, 0.0, 1.0 },
} };

namespace vkgsplat {

template<typename Ok> auto VKBResultToExpected(vkb::Result<Ok> &&res) -> std::expected<Ok, Error>
{
  if (!res) {
    auto message = res.detailed_failure_reasons() | std::views::join_with('\n') | std::ranges::to<std::string>();
    return std::unexpected{ Error{ res.error(), message } };
  }
  return std::move(res).value();
}

struct Init
{
  GLFWwindow *window{};
  vkb::Instance instance{};
  vkb::InstanceDispatchTable inst_disp;
  VkSurfaceKHR surface{};
  vkb::Device device{};
  vkb::DispatchTable disp;
  vkb::Swapchain swapchain{};
  vulkan::GPUAllocator gpu_allocator;
  PFN_vkWriteResourceDescriptorsEXT write_resource_descriptors{};
  PFN_vkCmdBindResourceHeapEXT cmd_bind_resource_heap{};
};

struct RenderData
{
  VkQueue graphics_queue{};
  VkQueue present_queue{};

  std::vector<VkImage> swapchain_images;
  std::vector<VkImageView> swapchain_image_views;

  VkPipeline graphics_pipeline{};

  vulkan::Buffer position_buffer{};
  vulkan::Buffer color_buffer{};
  vulkan::Buffer descriptor_heap_buffer{};
  VkDeviceSize descriptor_heap_size{};
  VkDeviceSize reserved_range_offset{};
  VkDeviceSize reserved_range_size{};
  size_t descriptor_stride{};

  VkCommandPool command_pool{};
  std::vector<VkCommandBuffer> command_buffers;

  std::vector<VkSemaphore> available_semaphores;
  std::vector<VkSemaphore> finished_semaphore;
  std::vector<VkFence> in_flight_fences;
  std::vector<VkFence> image_in_flight;
  size_t current_frame = {};
};

namespace {

auto align_up(VkDeviceSize value, VkDeviceSize alignment) -> VkDeviceSize
{ return (value + alignment - 1) / alignment * alignment; }

auto write_storage_buffer_descriptor(Init &init,
  VkDeviceAddress buffer_address,
  VkDeviceSize buffer_size,
  std::span<std::byte> destination) -> bool
{
  VkDeviceAddressRangeEXT const address_range = { .address = buffer_address, .size = buffer_size };
  VkResourceDescriptorInfoEXT resource_info{};
  resource_info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
  resource_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  resource_info.data.pAddressRange = &address_range;

  VkHostAddressRangeEXT const host_range = { .address = destination.data(), .size = destination.size() };

  return init.write_resource_descriptors(init.device, 1, &resource_info, &host_range) == VK_SUCCESS;
}

auto create_triangle_buffers(Init &init, RenderData &data) -> bool
{
  auto const position_buffer_size = static_cast<VkDeviceSize>(positions.size() * sizeof(positions.front()));
  auto const color_buffer_size = static_cast<VkDeviceSize>(colors.size() * sizeof(colors.front()));

  auto position_buffer = init.gpu_allocator.create_storage_buffer(position_buffer_size);
  auto color_buffer = init.gpu_allocator.create_storage_buffer(color_buffer_size);
  if (!position_buffer || !color_buffer) {
    std::println("Failed to create triangle vertex buffers!");
    return false;
  }

  std::span<const std::array<f32, 2>> const position_span{ positions };
  if (!init.gpu_allocator.write_buffer(*position_buffer, position_span)) {
    std::println("Failed to upload position buffer!");
    return false;
  }

  std::span<const std::array<f32, 3>> const color_span{ colors };
  if (!init.gpu_allocator.write_buffer(*color_buffer, color_span)) {
    std::println("Failed to upload color buffer!");
    return false;
  }

  VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
  heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

  VkPhysicalDeviceProperties2 props2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &heap_props,
    .properties = init.device.physical_device.properties,
  };
  init.inst_disp.getPhysicalDeviceProperties2(init.device.physical_device, &props2);

  auto const descriptor_size = static_cast<size_t>(heap_props.bufferDescriptorSize);
  data.descriptor_stride =
    static_cast<size_t>(align_up(heap_props.bufferDescriptorSize, heap_props.bufferDescriptorAlignment));
  VkDeviceSize const descriptor_region_size = data.descriptor_stride * 2;
  data.reserved_range_offset = align_up(descriptor_region_size, heap_props.resourceHeapAlignment);
  data.reserved_range_size = heap_props.minResourceHeapReservedRange;
  data.descriptor_heap_size = data.reserved_range_offset + data.reserved_range_size;

  auto descriptor_heap_buffer = init.gpu_allocator.create_heap_buffer(data.descriptor_heap_size);
  if (!descriptor_heap_buffer) {
    std::println("Failed to create triangle descriptor heap buffer!");
    return false;
  }

  std::vector<std::byte> descriptor_data(data.descriptor_stride * 2);
  std::array<VkDeviceAddressRangeEXT, 2> address_ranges = {
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(*position_buffer),
      .size = position_buffer_size,
    },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(*color_buffer),
      .size = color_buffer_size,
    },
  };

  for (size_t i = 0; i < 2; ++i) {
    if (!write_storage_buffer_descriptor(init,
          address_ranges.at(i).address,
          address_ranges.at(i).size,
          std::span{ descriptor_data }.subspan(i * data.descriptor_stride, descriptor_size))) {
      std::println("Failed to write triangle buffer descriptor {}!", i);
      return false;
    }
  }

  if (!init.gpu_allocator.write_buffer<std::byte>(*descriptor_heap_buffer, std::span{ descriptor_data })) {
    std::println("Failed to upload triangle descriptor heap!");
    return false;
  }

  data.position_buffer = *position_buffer;
  data.color_buffer = *color_buffer;
  data.descriptor_heap_buffer = *descriptor_heap_buffer;
  return true;
}

auto bind_triangle_descriptor_heap(Init const &init, RenderData const &data, VkCommandBuffer command_buffer) -> void
{
  VkDeviceAddress const heap_address = init.gpu_allocator.get_buffer_device_address(data.descriptor_heap_buffer);
  VkBindHeapInfoEXT const bind_heap_info = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = { .address = heap_address, .size = data.descriptor_heap_size },
    .reservedRangeOffset = data.reserved_range_offset,
    .reservedRangeSize = data.reserved_range_size,
  };
  init.cmd_bind_resource_heap(command_buffer, &bind_heap_info);
}

auto record_triangle_draw(Init const &init, RenderData const &data, VkCommandBuffer command_buffer, size_t image_index)
  -> void
{
  VkImageSubresourceRange const color_subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  auto color_barrier = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    data.swapchain_images.at(image_index),
    color_subresource_range);
  color_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &color_barrier);

  VkClearValue const clear_color{ { { 0.0F, 0.0F, 0.0F, 1.0F } } };
  VkRenderingAttachmentInfo const color_attachment = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = data.swapchain_image_views.at(image_index),
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .resolveMode = VK_RESOLVE_MODE_NONE,
    .resolveImageView = VK_NULL_HANDLE,
    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = clear_color,
  };

  VkRect2D const render_area{ .offset = { .x = 0, .y = 0 }, .extent = init.swapchain.extent };
  VkRenderingInfo const rendering_info = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .pNext = nullptr,
    .flags = 0,
    .renderArea = render_area,
    .layerCount = 1,
    .viewMask = 0,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment,
    .pDepthAttachment = nullptr,
    .pStencilAttachment = nullptr,
  };

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

  init.disp.cmdBeginRendering(command_buffer, &rendering_info);
  init.disp.cmdSetViewport(command_buffer, 0, 1, &viewport);
  init.disp.cmdSetScissor(command_buffer, 0, 1, &scissor);
  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);
  bind_triangle_descriptor_heap(init, data, command_buffer);
  init.disp.cmdDraw(command_buffer, 3, 1, 0, 0);
  init.disp.cmdEndRendering(command_buffer);

  auto present_barrier = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    data.swapchain_images.at(image_index),
    color_subresource_range);
  present_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &present_barrier);
}

}// namespace

GLFWwindow *create_window_glfw(const char *window_name = "", bool resize = true)
{
  if (glfwInit() == 0) {
    std::println("Failed to initialize GLFW");
    return nullptr;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  if (!resize) { glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); }

  auto const kWindowExtend = std::pair<int, int>{ 1024, 1024 };
  return glfwCreateWindow(kWindowExtend.first, kWindowExtend.second, window_name, nullptr, nullptr);
}

void destroy_window_glfw(GLFWwindow *window)
{
  glfwDestroyWindow(window);
  glfwTerminate();
}

VkSurfaceKHR create_surface_glfw(VkInstance instance, GLFWwindow *window, VkAllocationCallbacks *allocator = nullptr)
{
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkResult const err = glfwCreateWindowSurface(instance, window, allocator, &surface);
  if (0 != err) {
    char const *error_msg = nullptr;
    int const ret = glfwGetError(&error_msg);
    if (ret != 0) {
      std::cout << ret << " ";
      if (error_msg != nullptr) { std::cout << error_msg; }
      std::cout << "\n";
    }
    surface = VK_NULL_HANDLE;
  }
  return surface;
}

auto device_initialization(Init &init) -> std::expected<void, Error>
{
  init.window = create_window_glfw("Vulkan Triangle", true);

  VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{};
  descriptor_heap_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
  descriptor_heap_features.descriptorHeap = VK_TRUE;

  VkPhysicalDeviceVulkan12Features features_12{};
  features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features_12.bufferDeviceAddress = VK_TRUE;

  VkPhysicalDeviceVulkan13Features features_13{};
  features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features_13.dynamicRendering = VK_TRUE;

  vkb::InstanceBuilder instance_builder;
  return VKBResultToExpected(
    instance_builder.use_default_debug_messenger().request_validation_layers().require_api_version(1, 4, 0).build())
    .and_then([&](vkb::Instance const &instance) {
      init.instance = instance;
      init.inst_disp = init.instance.make_table();

      init.surface = create_surface_glfw(init.instance, init.window);

      vkb::PhysicalDeviceSelector phys_device_selector(init.instance);

      return VKBResultToExpected(phys_device_selector.set_surface(init.surface)
          .add_required_extension("VK_EXT_descriptor_heap")
          .add_required_extension("VK_KHR_buffer_device_address")
          .add_required_extension_features(descriptor_heap_features)
          .set_required_features_12(features_12)
          .set_required_features_13(features_13)
          .select());
    })
    .and_then([&](vkb::PhysicalDevice const &physical_device) {
      vkb::DeviceBuilder const device_builder{ physical_device };

      return VKBResultToExpected(device_builder.build());
    })
    .and_then([&](vkb::Device const &device) -> std::expected<void, Error> {
      init.device = device;
      init.disp = init.device.make_table();

      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      init.write_resource_descriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
        vkGetDeviceProcAddr(init.device, "vkWriteResourceDescriptorsEXT"));
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      init.cmd_bind_resource_heap = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(
        vkGetDeviceProcAddr(init.device, "vkCmdBindResourceHeapEXT"));

      if (init.write_resource_descriptors == nullptr || init.cmd_bind_resource_heap == nullptr) {
        return std::unexpected{ Error{ std::make_error_code(std::errc::function_not_supported),
          "VK_EXT_descriptor_heap entry points are unavailable" } };
      }

      return {};
    });
}

auto create_swapchain(Init &init) -> std::expected<void, Error>
{
  vkb::SwapchainBuilder swapchain_builder{ init.device };
  return VKBResultToExpected(swapchain_builder.set_old_swapchain(init.swapchain).build())
    .and_then([&](auto const &swapchain) {
      vkb::destroy_swapchain(init.swapchain);
      init.swapchain = swapchain;
      return std::expected<void, Error>{};
    });
}

auto get_queues(Init &init, RenderData &data) -> std::expected<void, Error>
{
  return VKBResultToExpected(init.device.get_queue(vkb::QueueType::graphics))
    .and_then([&](auto const &graphics_queue) {
      data.graphics_queue = graphics_queue;
      return VKBResultToExpected(init.device.get_queue(vkb::QueueType::present));
    })
    .and_then([&](auto const &present_queue) {
      data.present_queue = present_queue;
      return std::expected<void, Error>{};
    });
}

std::vector<char> readFile(const std::string &filename)
{
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    std::cout << "failed to open file!\n";
    return {};
  }

  auto file_size = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(file_size);

  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(file_size));

  file.close();

  return buffer;
}

VkShaderModule createShaderModule(Init &init, const std::vector<char> &code)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto const code_span = std::span{ reinterpret_cast<u32 const *>(code.data()), code.size() / sizeof(u32) };
  auto const create_info = initializers::ShaderModuleCreateInfo(code_span);

  VkShaderModule shaderModule = nullptr;
  if (init.disp.createShaderModule(&create_info, nullptr, &shaderModule) != VK_SUCCESS) {
    return VK_NULL_HANDLE;// failed to create shader module
  }

  return shaderModule;
}

int create_graphics_pipeline(Init &init, RenderData &data)
{
  auto vert_code = readFile(std::string(EXAMPLE_SOURCE_DIRECTORY) + "/shaders/triangle.vert.spv");
  auto frag_code = readFile(std::string(EXAMPLE_SOURCE_DIRECTORY) + "/shaders/triangle.frag.spv");

  VkShaderModule vert_module = nullptr;
  VkShaderModule frag_module = nullptr;
  vert_module = createShaderModule(init, vert_code);
  frag_module = createShaderModule(init, frag_code);

  if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
    std::cout << "failed to create shader module\n";
    return -1;// failed to create shader modules
  }

  VkPipelineShaderStageCreateInfo vert_stage_info =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vert_module, "main");

  std::array<VkDescriptorSetAndBindingMappingEXT, 1> vertex_mappings = { VkDescriptorSetAndBindingMappingEXT{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
    .pNext = nullptr,
    .descriptorSet = 0,
    .firstBinding = 0,
    .bindingCount = 2,
    .resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
    .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
    .sourceData = { .constantOffset = { .heapOffset = 0,
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

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
  // NOLINTBEGIN(hicpp-signed-bitwise)
  colorBlendAttachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  // NOLINTEND(hicpp-signed-bitwise)
  colorBlendAttachment.blendEnable = VK_FALSE;

  std::array<VkPipelineColorBlendAttachmentState, 1> color_blend_attachments = { colorBlendAttachment };
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
    return -1;// failed to create graphics pipeline
  }

  init.disp.destroyShaderModule(frag_module, nullptr);
  init.disp.destroyShaderModule(vert_module, nullptr);
  return 0;
}

int create_swapchain_images(Init &init, RenderData &data)
{
  data.swapchain_images = init.swapchain.get_images().value();
  data.swapchain_image_views = init.swapchain.get_image_views().value();
  return 0;
}

int create_command_pool(Init &init, RenderData &data)
{
  auto const pool_info = initializers::CommandPoolCreateInfo(
    static_cast<u32>(init.device.get_queue_index(vkb::QueueType::graphics).value()));

  if (init.disp.createCommandPool(&pool_info, nullptr, &data.command_pool) != VK_SUCCESS) {
    std::cout << "failed to create command pool\n";
    return -1;// failed to create command pool
  }
  return 0;
}

int create_command_buffers(Init &init, RenderData &data)
{
  data.command_buffers.resize(data.swapchain_image_views.size());

  auto const alloc_info = initializers::CommandBufferAllocateInfo(
    data.command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, static_cast<u32>(data.command_buffers.size()));

  if (init.disp.allocateCommandBuffers(&alloc_info, data.command_buffers.data()) != VK_SUCCESS) {
    return -1;// failed to allocate command buffers;
  }

  for (size_t i = 0; i < data.command_buffers.size(); i++) {
    auto const begin_info = initializers::CommandBufferBeginInfo();

    if (init.disp.beginCommandBuffer(data.command_buffers.at(i), &begin_info) != VK_SUCCESS) {
      return -1;// failed to begin recording command buffer
    }

    record_triangle_draw(init, data, data.command_buffers.at(i), i);

    if (init.disp.endCommandBuffer(data.command_buffers.at(i)) != VK_SUCCESS) {
      std::cout << "failed to record command buffer\n";
      return -1;// failed to record command buffer!
    }
  }
  return 0;
}

int create_sync_objects(Init &init, RenderData &data)
{
  data.available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
  data.finished_semaphore.resize(init.swapchain.image_count);
  data.in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);
  data.image_in_flight.resize(init.swapchain.image_count, VK_NULL_HANDLE);

  auto const semaphore_info = initializers::SemaphoreCreateInfo();
  auto const fence_info = initializers::FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);

  for (size_t i = 0; i < init.swapchain.image_count; i++) {
    if (init.disp.createSemaphore(&semaphore_info, nullptr, &data.finished_semaphore.at(i)) != VK_SUCCESS) {
      std::cout << "failed to create sync objects\n";
      return -1;// failed to create synchronization objects for a frame
    }
  }

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (init.disp.createSemaphore(&semaphore_info, nullptr, &data.available_semaphores.at(i)) != VK_SUCCESS
        || init.disp.createFence(&fence_info, nullptr, &data.in_flight_fences.at(i)) != VK_SUCCESS) {
      std::cout << "failed to create sync objects\n";
      return -1;// failed to create synchronization objects for a frame
    }
  }
  return 0;
}

int recreate_swapchain(Init &init, RenderData &data)
{
  init.disp.deviceWaitIdle();

  init.disp.destroyCommandPool(data.command_pool, nullptr);
  init.disp.destroyPipeline(data.graphics_pipeline, nullptr);

  init.swapchain.destroy_image_views(data.swapchain_image_views);

  if (!create_swapchain(init).has_value()) { return -1; }
  if (0 != create_graphics_pipeline(init, data)) { return -1; }
  if (0 != create_swapchain_images(init, data)) { return -1; }
  if (0 != create_command_pool(init, data)) { return -1; }
  if (0 != create_command_buffers(init, data)) { return -1; }
  return 0;
}
namespace compute {

  auto align_up(VkDeviceSize value, VkDeviceSize alignment) -> VkDeviceSize
  { return (value + alignment - 1) / alignment * alignment; }

  auto write_storage_buffer_descriptor(Init &init,
    VkDeviceAddress buffer_address,
    VkDeviceSize buffer_size,
    std::span<std::byte> destination) -> bool
  {
    VkDeviceAddressRangeEXT const address_range = { .address = buffer_address, .size = buffer_size };
    VkResourceDescriptorInfoEXT resource_info{};
    resource_info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    resource_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    resource_info.data.pAddressRange = &address_range;

    VkHostAddressRangeEXT const host_range = { .address = destination.data(), .size = destination.size() };

    return init.write_resource_descriptors(init.device, 1, &resource_info, &host_range) == VK_SUCCESS;
  }

  void run_compute_test(Init &init)
  {
    std::println("--- Rozpoczynam test Compute Shadera (Dodawanie 2 tablic) ---");

    auto compute_queue_res = init.device.get_queue(vkb::QueueType::compute);
    if (!compute_queue_res) {
      std::println("Brak kolejki obliczeniowej!");
      return;
    }
    VkQueue compute_queue = compute_queue_res.value();

    uint32_t const element_count = 1024;
    VkDeviceSize const buffer_size = element_count * sizeof(float);

    auto bufferA = init.gpu_allocator.create_storage_buffer(buffer_size);
    auto bufferB = init.gpu_allocator.create_storage_buffer(buffer_size);
    auto bufferResult = init.gpu_allocator.create_storage_buffer(buffer_size);
    if (!bufferA || !bufferB || !bufferResult) {
      std::println("Nie udało się utworzyć buforów!");
      return;
    }

    auto const input = std::views::iota(0U, element_count) | std::ranges::to<std::vector<float>>();

    if (!init.gpu_allocator.write_buffer(*bufferA, std::span{ input })) {
      std::println("Nie udało się zapisać danych do bufora A!");
      return;
    }

    if (!init.gpu_allocator.write_buffer(*bufferB, std::span{ input })) {
      std::println("Nie udało się zapisać danych do bufora B!");
      return;
    }

    VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
    heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

    VkPhysicalDeviceProperties2 props2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &heap_props,
      .properties = init.device.physical_device.properties,
    };
    init.inst_disp.getPhysicalDeviceProperties2(init.device.physical_device, &props2);

    auto const descriptor_size = static_cast<size_t>(heap_props.bufferDescriptorSize);
    auto const descriptor_stride =
      static_cast<size_t>(align_up(heap_props.bufferDescriptorSize, heap_props.bufferDescriptorAlignment));
    VkDeviceSize const descriptor_region_size = descriptor_stride * 3;
    VkDeviceSize const reserved_range_offset = align_up(descriptor_region_size, heap_props.resourceHeapAlignment);
    VkDeviceSize const reserved_range_size = heap_props.minResourceHeapReservedRange;
    VkDeviceSize const heap_size = reserved_range_offset + reserved_range_size;

    auto bufferHeap = init.gpu_allocator.create_heap_buffer(heap_size);
    if (!bufferHeap) {
      std::println("Failed to create a heap buffer!");
      return;
    }

    std::vector<std::byte> descriptor_data(descriptor_stride * 3);
    std::array<VkDeviceAddressRangeEXT, 3> address_ranges = {
      VkDeviceAddressRangeEXT{ .address = init.gpu_allocator.get_buffer_device_address(*bufferA), .size = buffer_size },
      VkDeviceAddressRangeEXT{ .address = init.gpu_allocator.get_buffer_device_address(*bufferB), .size = buffer_size },
      VkDeviceAddressRangeEXT{
        .address = init.gpu_allocator.get_buffer_device_address(*bufferResult), .size = buffer_size },
    };

    for (size_t i = 0; i < 3; ++i) {
      if (!write_storage_buffer_descriptor(init,
            address_ranges.at(i).address,
            address_ranges.at(i).size,
            std::span{ descriptor_data }.subspan(i * descriptor_stride, descriptor_size))) {
        std::println("Nie udało się zapisać deskryptora {}!", i);
        return;
      }
    }

    if (!init.gpu_allocator.write_buffer<std::byte>(*bufferHeap, std::span{ descriptor_data })) {
      std::println("Nie udało się przesłać sterty deskryptorów na GPU!");
      return;
    }

    auto comp_code = readFile(std::string(EXAMPLE_SOURCE_DIRECTORY) + "/shaders/1plus1.comp.spv");
    VkShaderModule comp_module = createShaderModule(init, comp_code);
    if (comp_module == VK_NULL_HANDLE) {
      std::println("Nie udało się utworzyć modułu shadera!");
      return;
    }

    std::array<VkDescriptorSetAndBindingMappingEXT, 1> mappings = { VkDescriptorSetAndBindingMappingEXT{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
      .pNext = nullptr,
      .descriptorSet = 0,
      .firstBinding = 0,
      .bindingCount = 3,
      .resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT
                      | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
      .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
      .sourceData = { .constantOffset = { .heapOffset = 0,
                        .heapArrayStride = static_cast<uint32_t>(descriptor_stride),
                        .pEmbeddedSampler = nullptr,
                        .samplerHeapOffset = 0,
                        .samplerHeapArrayStride = 0 } },
    } };

    VkShaderDescriptorSetAndBindingMappingInfoEXT mapping_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
      .pNext = nullptr,
      .mappingCount = static_cast<uint32_t>(mappings.size()),
      .pMappings = mappings.data(),
    };

    VkPipelineShaderStageCreateInfo const comp_stage =
      initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, comp_module, "main");
    VkPipelineShaderStageCreateInfo stage = comp_stage;
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

    VkPipeline compute_pipeline = VK_NULL_HANDLE;
    if (init.disp.createComputePipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &compute_pipeline) != VK_SUCCESS) {
      std::println("Nie udało się utworzyć potoku obliczeniowego!");
      init.disp.destroyShaderModule(comp_module, nullptr);
      return;
    }

    auto const compute_queue_index_res = init.device.get_queue_index(vkb::QueueType::compute);
    if (!compute_queue_index_res) {
      std::println("Brak indeksu kolejki obliczeniowej!");
      init.disp.destroyPipeline(compute_pipeline, nullptr);
      init.disp.destroyShaderModule(comp_module, nullptr);
      return;
    }

    auto const cmd_pool_info = initializers::CommandPoolCreateInfo(compute_queue_index_res.value());

    VkCommandPool command_pool = VK_NULL_HANDLE;
    if (init.disp.createCommandPool(&cmd_pool_info, nullptr, &command_pool) != VK_SUCCESS) {
      std::println("Nie udało się utworzyć puli komend!");
      init.disp.destroyPipeline(compute_pipeline, nullptr);
      init.disp.destroyShaderModule(comp_module, nullptr);
      return;
    }

    auto const cmd_alloc_info =
      initializers::CommandBufferAllocateInfo(command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (init.disp.allocateCommandBuffers(&cmd_alloc_info, &command_buffer) != VK_SUCCESS) {
      std::println("Nie udało się zaalokować bufora komend!");
      init.disp.destroyCommandPool(command_pool, nullptr);
      init.disp.destroyPipeline(compute_pipeline, nullptr);
      init.disp.destroyShaderModule(comp_module, nullptr);
      return;
    }

    VkDeviceAddress const heap_address = init.gpu_allocator.get_buffer_device_address(*bufferHeap);
    VkBindHeapInfoEXT const bind_heap_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
      .pNext = nullptr,
      .heapRange = { .address = heap_address, .size = heap_size },
      .reservedRangeOffset = reserved_range_offset,
      .reservedRangeSize = reserved_range_size,
    };

    auto const begin_info = initializers::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    init.disp.beginCommandBuffer(command_buffer, &begin_info);
    init.cmd_bind_resource_heap(command_buffer, &bind_heap_info);
    init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
    init.disp.cmdDispatch(command_buffer, element_count / 64, 1, 1);// NOLINT
    init.disp.endCommandBuffer(command_buffer);

    auto const submit_info = initializers::SubmitInfo({}, {}, std::span{ &command_buffer, 1 }, {});

    init.disp.queueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE);
    init.disp.queueWaitIdle(compute_queue);

    auto output = init.gpu_allocator.read_buffer<float>(*bufferResult, element_count);
    if (!output) {
      std::println("Nie udało się odczytać bufora wynikowego!");
      init.disp.destroyCommandPool(command_pool, nullptr);
      init.disp.destroyPipeline(compute_pipeline, nullptr);
      init.disp.destroyShaderModule(comp_module, nullptr);
      return;
    }

    std::println("Wyniki dodawania (pierwsze 5 z 1024):");
    for (size_t i{}; i < 5; i++) {// NOLINT
      std::println("Index {}: {} + {} = {}", i, input[i], input[i], output->at(i));// NOLINT
    }

    init.disp.destroyShaderModule(comp_module, nullptr);
    init.disp.destroyPipeline(compute_pipeline, nullptr);
    init.disp.destroyCommandPool(command_pool, nullptr);

    init.gpu_allocator.destroy_buffer(*bufferA);
    init.gpu_allocator.destroy_buffer(*bufferB);
    init.gpu_allocator.destroy_buffer(*bufferResult);
    init.gpu_allocator.destroy_buffer(*bufferHeap);

    std::println("--- Test dodawania dwóch tablic zakończony ---");
  }


}// namespace compute

int draw_frame(Init &init, RenderData &data)
{
  init.disp.waitForFences(1, &data.in_flight_fences.at(data.current_frame), VK_TRUE, UINT64_MAX);

  uint32_t image_index = 0;
  VkResult result = init.disp.acquireNextImageKHR(
    init.swapchain, UINT64_MAX, data.available_semaphores.at(data.current_frame), VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return recreate_swapchain(init, data);
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    std::cout << "failed to acquire swapchain image. Error " << result << "\n";
    return -1;
  }

  if (data.image_in_flight.at(image_index) != VK_NULL_HANDLE) {
    init.disp.waitForFences(1, &data.image_in_flight.at(image_index), VK_TRUE, UINT64_MAX);
  }
  data.image_in_flight.at(image_index) = data.in_flight_fences.at(data.current_frame);

  std::array<VkSemaphore, 1> wait_semaphores = { data.available_semaphores.at(data.current_frame) };
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  std::array<VkSemaphore, 1> signal_semaphores = { data.finished_semaphore.at(image_index) };

  auto const submit_info = initializers::SubmitInfo(
    wait_semaphores, wait_stages, std::span{ &data.command_buffers.at(image_index), 1 }, signal_semaphores);

  init.disp.resetFences(1, &data.in_flight_fences.at(data.current_frame));

  if (init.disp.queueSubmit(data.graphics_queue, 1, &submit_info, data.in_flight_fences.at(data.current_frame))
      != VK_SUCCESS) {
    std::cout << "failed to submit draw command buffer\n";
    return -1;//"failed to submit draw command buffer
  }

  std::array<VkSwapchainKHR, 1> const swap_chains = { init.swapchain };
  auto const present_info = initializers::PresentInfoKHR(signal_semaphores, swap_chains, std::span{ &image_index, 1 });

  result = init.disp.queuePresentKHR(data.present_queue, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return recreate_swapchain(init, data);
  } else if (result != VK_SUCCESS) {
    std::cout << "failed to present swapchain image\n";
    return -1;
  }

  data.current_frame = (data.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
  return 0;
}

void cleanup(Init &init, RenderData &data)
{
  for (size_t i = 0; i < init.swapchain.image_count; i++) {
    init.disp.destroySemaphore(data.finished_semaphore.at(i), nullptr);
  }
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    init.disp.destroySemaphore(data.available_semaphores.at(i), nullptr);
    init.disp.destroyFence(data.in_flight_fences.at(i), nullptr);
  }

  init.disp.destroyCommandPool(data.command_pool, nullptr);

  init.gpu_allocator.destroy_buffer(data.position_buffer);
  init.gpu_allocator.destroy_buffer(data.color_buffer);
  init.gpu_allocator.destroy_buffer(data.descriptor_heap_buffer);

  init.disp.destroyPipeline(data.graphics_pipeline, nullptr);

  init.swapchain.destroy_image_views(data.swapchain_image_views);

  vkb::destroy_swapchain(init.swapchain);
  vkb::destroy_device(init.device);
  vkb::destroy_surface(init.instance, init.surface);
  vkb::destroy_instance(init.instance);
  destroy_window_glfw(init.window);
}

[[nodiscard]] int run() noexcept
{
  try {
    Init init;
    RenderData render_data;

    auto const init_result = device_initialization(init);
    if (!init_result.has_value()) {
      std::println("Inicjalizacja urządzenia nie powiodła się: {}", init_result.error().message());
      return -1;
    }

    auto gpu_allocator = vulkan::GPUAllocator::create(init.instance, init.device, init.device.physical_device);
    if (!gpu_allocator) {
      std::println("Nie udało się utworzyć alokatora GPU!");
      return -1;
    }
    init.gpu_allocator = std::move(*gpu_allocator);

    compute::run_compute_test(init);
    if (!create_swapchain(init).has_value()) { return -1; }
    if (!get_queues(init, render_data).has_value()) { return -1; }
    if (!create_triangle_buffers(init, render_data)) { return -1; }
    if (0 != create_graphics_pipeline(init, render_data)) { return -1; }
    if (0 != create_swapchain_images(init, render_data)) { return -1; }
    if (0 != create_command_pool(init, render_data)) { return -1; }
    if (0 != create_command_buffers(init, render_data)) { return -1; }
    if (0 != create_sync_objects(init, render_data)) { return -1; }

    while (0 == glfwWindowShouldClose(init.window)) {
      glfwPollEvents();
      int const res = draw_frame(init, render_data);
      if (res != 0) {
        std::cout << "failed to draw frame \n";
        return -1;
      }
    }
    init.disp.deviceWaitIdle();

    cleanup(init, render_data);
  } catch (std::exception const &exception) {
    return -1;
  } catch (...) {
    return -1;
  }
  return 0;
}

}// namespace vkgsplat

int main() { return vkgsplat::run(); }
