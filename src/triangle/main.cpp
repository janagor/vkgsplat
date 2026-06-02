// based on https://github.com/charles-lunarg/vk-bootstrap/blob/main/example/triangle.cpp
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

#include "example_config.h"

const int MAX_FRAMES_IN_FLIGHT = 2;

struct Init
{
  GLFWwindow *window{};
  vkb::Instance instance{};
  vkb::InstanceDispatchTable inst_disp;
  VkSurfaceKHR surface{};
  vkb::Device device{};
  vkb::DispatchTable disp;
  vkb::Swapchain swapchain{};
};

struct RenderData
{
  VkQueue graphics_queue{};
  VkQueue present_queue{};

  std::vector<VkImage> swapchain_images;
  std::vector<VkImageView> swapchain_image_views;
  std::vector<VkFramebuffer> framebuffers;

  VkRenderPass render_pass{};
  VkPipelineLayout pipeline_layout{};
  VkPipeline graphics_pipeline{};

  VkCommandPool command_pool{};
  std::vector<VkCommandBuffer> command_buffers;

  std::vector<VkSemaphore> available_semaphores;
  std::vector<VkSemaphore> finished_semaphore;
  std::vector<VkFence> in_flight_fences;
  std::vector<VkFence> image_in_flight;
  size_t current_frame = {};
};

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

int device_initialization(Init &init)
{
  init.window = create_window_glfw("Vulkan Triangle", true);

  vkb::InstanceBuilder instance_builder;
  auto instance_ret = instance_builder.use_default_debug_messenger().request_validation_layers().build();
  if (!instance_ret) {
    std::cout << instance_ret.error().message() << "\n";
    return -1;
  }
  init.instance = instance_ret.value();

  init.inst_disp = init.instance.make_table();

  init.surface = create_surface_glfw(init.instance, init.window);

  vkb::PhysicalDeviceSelector phys_device_selector(init.instance);

  auto phys_device_ret = phys_device_selector.set_surface(init.surface).select();
  if (!phys_device_ret) {
    std::cout << phys_device_ret.error().message() << "\n";
    if (phys_device_ret.error() == vkb::PhysicalDeviceError::no_suitable_device) {
      const auto &detailed_reasons = phys_device_ret.detailed_failure_reasons();
      if (!detailed_reasons.empty()) {
        std::cerr << "GPU Selection failure reasons:\n";
        for (const std::string &reason : detailed_reasons) { std::cerr << reason << "\n"; }
      }
    }
    return -1;
  }
  vkb::PhysicalDevice const &physical_device = phys_device_ret.value();

  vkb::DeviceBuilder const device_builder{ physical_device };

  auto device_ret = device_builder.build();
  if (!device_ret) {
    std::cout << device_ret.error().message() << "\n";
    return -1;
  }
  init.device = device_ret.value();

  init.disp = init.device.make_table();

  return 0;
}

int create_swapchain(Init &init)
{
  vkb::SwapchainBuilder swapchain_builder{ init.device };
  auto swap_ret = swapchain_builder.set_old_swapchain(init.swapchain).build();
  if (!swap_ret) {
    std::cout << swap_ret.error().message() << " " << swap_ret.vk_result() << "\n";
    return -1;
  }
  vkb::destroy_swapchain(init.swapchain);
  init.swapchain = swap_ret.value();
  return 0;
}

int get_queues(Init &init, RenderData &data)
{
  auto const graphics_queue = init.device.get_queue(vkb::QueueType::graphics);
  if (!graphics_queue.has_value()) {
    std::cout << "failed to get graphics queue: " << graphics_queue.error().message() << "\n";
    return -1;
  }
  data.graphics_queue = graphics_queue.value();

  auto const present_queue = init.device.get_queue(vkb::QueueType::present);
  if (!present_queue.has_value()) {
    std::cout << "failed to get present queue: " << present_queue.error().message() << "\n";
    return -1;
  }
  data.present_queue = present_queue.value();
  return 0;
}

int create_render_pass(Init &init, RenderData &data)
{
  VkAttachmentDescription const color_attachment = {
    .flags = VK_FORMAT_UNDEFINED,
    .format = init.swapchain.image_format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  VkAttachmentReference color_attachment_ref = {};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription const subpass = { .flags = VK_FORMAT_UNDEFINED,
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .inputAttachmentCount = 0,
    .pInputAttachments = nullptr,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment_ref,
    .pResolveAttachments = nullptr,
    .pDepthStencilAttachment = nullptr,
    .preserveAttachmentCount = 0,
    .pPreserveAttachments = nullptr };

  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo const render_pass_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_FORMAT_UNDEFINED,
    .attachmentCount = 1,
    .pAttachments = &color_attachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 1,
    .pDependencies = &dependency,
  };

  if (init.disp.createRenderPass(&render_pass_info, nullptr, &data.render_pass) != VK_SUCCESS) {
    std::cout << "failed to create render pass\n";
    return -1;// failed to create render pass!
  }
  return 0;
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
  VkShaderModuleCreateInfo create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size();
  // TODO: replace this reinterpret_cast with someting else
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  create_info.pCode = reinterpret_cast<const uint32_t *>(code.data());

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

  VkPipelineShaderStageCreateInfo const vert_stage_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_FORMAT_UNDEFINED,
    .stage = VK_SHADER_STAGE_VERTEX_BIT,
    .module = vert_module,
    .pName = "main",
    .pSpecializationInfo = nullptr,
  };

  VkPipelineShaderStageCreateInfo const frag_stage_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_FORMAT_UNDEFINED,
    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
    .module = frag_module,
    .pName = "main",
    .pSpecializationInfo = nullptr,
  };

  std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = { vert_stage_info, frag_stage_info };

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
  vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_info.vertexBindingDescriptionCount = 0;
  vertex_input_info.vertexAttributeDescriptionCount = 0;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

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

  VkPipelineViewportStateCreateInfo const viewport_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_FORMAT_UNDEFINED,
    .viewportCount = 1,
    .pViewports = &viewport,
    .scissorCount = 1,
    .pScissors = &scissor,
  };

  VkPipelineRasterizationStateCreateInfo const rasterizer = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_FORMAT_UNDEFINED,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,

    .depthBiasConstantFactor = 0.0F,
    .depthBiasClamp = 0.0F,
    .depthBiasSlopeFactor = 0.0F,

    .lineWidth = 1.0F,
  };

  VkPipelineMultisampleStateCreateInfo const multisampling = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_FORMAT_UNDEFINED,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,

    .minSampleShading = 1.0F,
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE,
  };

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
  // NOLINTBEGIN(hicpp-signed-bitwise)
  colorBlendAttachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  // NOLINTEND(hicpp-signed-bitwise)
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo color_blending = {};
  color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.logicOp = VK_LOGIC_OP_COPY;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &colorBlendAttachment;
  color_blending.blendConstants[0] = 0.0F;
  color_blending.blendConstants[1] = 0.0F;
  color_blending.blendConstants[2] = 0.0F;
  color_blending.blendConstants[3] = 0.0F;

  VkPipelineLayoutCreateInfo pipeline_layout_info = {};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 0;
  pipeline_layout_info.pushConstantRangeCount = 0;

  if (init.disp.createPipelineLayout(&pipeline_layout_info, nullptr, &data.pipeline_layout) != VK_SUCCESS) {
    std::cout << "failed to create pipeline layout\n";
    return -1;// failed to create pipeline layout
  }

  std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  VkPipelineDynamicStateCreateInfo dynamic_info = {};
  dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
  dynamic_info.pDynamicStates = dynamic_states.data();

  VkGraphicsPipelineCreateInfo pipeline_info = {};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages.data();
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_info;
  pipeline_info.layout = data.pipeline_layout;
  pipeline_info.renderPass = data.render_pass;
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

int create_framebuffers(Init &init, RenderData &data)
{
  data.swapchain_images = init.swapchain.get_images().value();
  data.swapchain_image_views = init.swapchain.get_image_views().value();

  data.framebuffers.resize(data.swapchain_image_views.size());

  for (size_t i = 0; i < data.swapchain_image_views.size(); i++) {
    std::array<VkImageView, 1> attachments = { data.swapchain_image_views.at(i) };

    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = data.render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = attachments.data();
    framebuffer_info.width = init.swapchain.extent.width;
    framebuffer_info.height = init.swapchain.extent.height;
    framebuffer_info.layers = 1;

    if (init.disp.createFramebuffer(&framebuffer_info, nullptr, &data.framebuffers.at(i)) != VK_SUCCESS) {
      return -1;// failed to create framebuffer
    }
  }
  return 0;
}

int create_command_pool(Init &init, RenderData &data)
{
  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.queueFamilyIndex = init.device.get_queue_index(vkb::QueueType::graphics).value();

  if (init.disp.createCommandPool(&pool_info, nullptr, &data.command_pool) != VK_SUCCESS) {
    std::cout << "failed to create command pool\n";
    return -1;// failed to create command pool
  }
  return 0;
}

int create_command_buffers(Init &init, RenderData &data)
{
  data.command_buffers.resize(data.framebuffers.size());

  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = data.command_pool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(data.command_buffers.size());

  if (init.disp.allocateCommandBuffers(&allocInfo, data.command_buffers.data()) != VK_SUCCESS) {
    return -1;// failed to allocate command buffers;
  }

  for (size_t i = 0; i < data.command_buffers.size(); i++) {
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (init.disp.beginCommandBuffer(data.command_buffers.at(i), &begin_info) != VK_SUCCESS) {
      return -1;// failed to begin recording command buffer
    }

    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = data.render_pass;
    render_pass_info.framebuffer = data.framebuffers.at(i);
    render_pass_info.renderArea.offset = { .x = 0, .y = 0 };
    render_pass_info.renderArea.extent = init.swapchain.extent;
    VkClearValue const clearColor{ { { 0.0F, 0.0F, 0.0F, 1.0F } } };
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clearColor;

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

    init.disp.cmdSetViewport(data.command_buffers.at(i), 0, 1, &viewport);
    init.disp.cmdSetScissor(data.command_buffers.at(i), 0, 1, &scissor);

    init.disp.cmdBeginRenderPass(data.command_buffers.at(i), &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    init.disp.cmdBindPipeline(data.command_buffers.at(i), VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);

    init.disp.cmdDraw(data.command_buffers.at(i), 3, 1, 0, 0);

    init.disp.cmdEndRenderPass(data.command_buffers.at(i));

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

  VkSemaphoreCreateInfo semaphore_info = {};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

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

  for (auto *framebuffer : data.framebuffers) { init.disp.destroyFramebuffer(framebuffer, nullptr); }

  init.swapchain.destroy_image_views(data.swapchain_image_views);

  if (0 != create_swapchain(init)) { return -1; }
  if (0 != create_framebuffers(init, data)) { return -1; }
  if (0 != create_command_pool(init, data)) { return -1; }
  if (0 != create_command_buffers(init, data)) { return -1; }
  return 0;
}

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

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  std::array<VkSemaphore, 1> wait_semaphores = { data.available_semaphores.at(data.current_frame) };
  std::array<VkPipelineStageFlags, 1> wait_stages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = wait_semaphores.data();
  submitInfo.pWaitDstStageMask = wait_stages.data();

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &data.command_buffers.at(image_index);

  std::array<VkSemaphore, 1> signal_semaphores = { data.finished_semaphore.at(image_index) };
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signal_semaphores.data();

  init.disp.resetFences(1, &data.in_flight_fences.at(data.current_frame));

  if (init.disp.queueSubmit(data.graphics_queue, 1, &submitInfo, data.in_flight_fences.at(data.current_frame))
      != VK_SUCCESS) {
    std::cout << "failed to submit draw command buffer\n";
    return -1;//"failed to submit draw command buffer
  }

  VkPresentInfoKHR present_info = {};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores.data();

  std::array<VkSwapchainKHR, 1> const swapChains = { init.swapchain };
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swapChains.data();

  present_info.pImageIndices = &image_index;

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

  for (auto *framebuffer : data.framebuffers) { init.disp.destroyFramebuffer(framebuffer, nullptr); }

  init.disp.destroyPipeline(data.graphics_pipeline, nullptr);
  init.disp.destroyPipelineLayout(data.pipeline_layout, nullptr);
  init.disp.destroyRenderPass(data.render_pass, nullptr);

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

    if (0 != device_initialization(init)) { return -1; }
    if (0 != create_swapchain(init)) { return -1; }
    if (0 != get_queues(init, render_data)) { return -1; }
    if (0 != create_render_pass(init, render_data)) { return -1; }
    if (0 != create_graphics_pipeline(init, render_data)) { return -1; }
    if (0 != create_framebuffers(init, render_data)) { return -1; }
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

int main() { return run(); }
