// based on https://github.com/charles-lunarg/vk-bootstrap/blob/main/example/triangle.cpp
#include <array>
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

auto device_initialization(Init &init) -> std::expected<void, Error>
{
  init.window = create_window_glfw("Vulkan Triangle", true);

  vkb::InstanceBuilder instance_builder;
  return VKBResultToExpected(
    instance_builder.use_default_debug_messenger().request_validation_layers().require_api_version(1, 4, 0).build())
    .and_then([&](vkb::Instance const &instance) {
      init.instance = instance;
      init.inst_disp = init.instance.make_table();

      init.surface = create_surface_glfw(init.instance, init.window);

      vkb::PhysicalDeviceSelector phys_device_selector(init.instance);

      return VKBResultToExpected(phys_device_selector.set_surface(init.surface).select());
    })
    .and_then([&](vkb::PhysicalDevice const &physical_device) {
      vkb::DeviceBuilder const device_builder{ physical_device };

      return VKBResultToExpected(device_builder.build());
    })
    .and_then([&](vkb::Device const &device) {
      init.device = device;
      init.disp = init.device.make_table();
      return std::expected<void, Error>{};
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

  auto const render_pass_info = initializers::RenderPassCreateInfo(
    std::span{ &color_attachment, 1 }, std::span{ &subpass, 1 }, std::span{ &dependency, 1 });

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
  viewport.width = static_cast<float>(init.swapchain.extent.width);
  viewport.height = static_cast<float>(init.swapchain.extent.height);
  viewport.minDepth = 0.0F;
  viewport.maxDepth = 1.0F;

  VkRect2D scissor = {};
  scissor.offset = { .x = 0, .y = 0 };
  scissor.extent = init.swapchain.extent;

  auto const viewport_state = initializers::PipelineViewportStateCreateInfo(
    std::span{ &viewport, 1 }, std::span{ &scissor, 1 });

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

  auto const pipeline_layout_info = initializers::PipelineLayoutCreateInfo({}, {});

  if (init.disp.createPipelineLayout(&pipeline_layout_info, nullptr, &data.pipeline_layout) != VK_SUCCESS) {
    std::cout << "failed to create pipeline layout\n";
    return -1;// failed to create pipeline layout
  }

  std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  auto dynamic_info = initializers::PipelineDynamicStateCreateInfo(dynamic_states);

  auto pipeline_info = initializers::GraphicsPipelineCreateInfo();
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

    auto const framebuffer_info =
      initializers::FramebufferCreateInfo(data.render_pass, attachments, init.swapchain.extent, 1);

    if (init.disp.createFramebuffer(&framebuffer_info, nullptr, &data.framebuffers.at(i)) != VK_SUCCESS) {
      return -1;// failed to create framebuffer
    }
  }
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
  data.command_buffers.resize(data.framebuffers.size());

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

    VkClearValue const clear_color{ { { 0.0F, 0.0F, 0.0F, 1.0F } } };
    VkRect2D const render_area{ .offset = { .x = 0, .y = 0 }, .extent = init.swapchain.extent };
    auto const render_pass_info = initializers::RenderPassBeginInfo(
      data.render_pass, data.framebuffers.at(i), render_area, std::span{ &clear_color, 1 });

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

  for (auto *framebuffer : data.framebuffers) { init.disp.destroyFramebuffer(framebuffer, nullptr); }

  init.swapchain.destroy_image_views(data.swapchain_image_views);

  if (!create_swapchain(init).has_value()) { return -1; }
  if (0 != create_framebuffers(init, data)) { return -1; }
  if (0 != create_command_pool(init, data)) { return -1; }
  if (0 != create_command_buffers(init, data)) { return -1; }
  return 0;
}
namespace compute {

  void run_compute_test(Init &init)
  {
    std::println("--- Rozpoczynam test Compute Shadera (Dodawanie 2 tablic) ---");

    auto compute_queue_res = init.device.get_queue(vkb::QueueType::compute);
    if (!compute_queue_res) {
      std::println("Brak kolejki obliczeniowej!");
      return;
    }
    VkQueue compute_queue = compute_queue_res.value();

    const uint32_t element_count = 1024;
    const VkDeviceSize buffer_size = element_count * sizeof(float);

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

    // 4. Deskryptory - Tym razem mamy 3 bindingi!
    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {};
    for (size_t i = 0; i < 3; i++) {
      bindings.at(i).binding = static_cast<uint32_t>(i);// Binding 0, 1, 2
      bindings.at(i).descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings.at(i).descriptorCount = 1;
      bindings.at(i).stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    auto const layout_info = initializers::DescriptorSetLayoutCreateInfo(bindings);

    VkDescriptorSetLayout descriptor_layout = nullptr;
    init.disp.createDescriptorSetLayout(&layout_info, nullptr, &descriptor_layout);

    // Potrzebujemy puli na 3 deskryptory typu Storage Buffer
    VkDescriptorPoolSize const pool_size = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 3 };
    std::array<VkDescriptorPoolSize, 1> pool_sizes = { pool_size };
    auto const pool_info = initializers::DescriptorPoolCreateInfo(pool_sizes, 1);

    VkDescriptorPool descriptor_pool = nullptr;
    init.disp.createDescriptorPool(&pool_info, nullptr, &descriptor_pool);

    std::array<VkDescriptorSetLayout, 1> descriptor_layouts = { descriptor_layout };
    auto const set_alloc_info = initializers::DescriptorSetAllocateInfo(descriptor_pool, descriptor_layouts);

    VkDescriptorSet descriptor_set = nullptr;
    init.disp.allocateDescriptorSets(&set_alloc_info, &descriptor_set);

    // Łączymy bufory z konkretnymi bindingami
    std::array<VkDescriptorBufferInfo, 3> buffer_infos = {};
    buffer_infos.at(0) = { .buffer = bufferA->handle, .offset = 0, .range = buffer_size };
    buffer_infos.at(1) = { .buffer = bufferB->handle, .offset = 0, .range = buffer_size };
    buffer_infos.at(2) = { .buffer = bufferResult->handle, .offset = 0, .range = buffer_size };

    std::array<VkWriteDescriptorSet, 3> writes = {};
    for (u32 i = 0; i < 3; i++) {
      writes.at(i) = initializers::WriteDescriptorSet(
        descriptor_set, i, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, std::span{ &buffer_infos.at(i), 1 });
    }
    init.disp.updateDescriptorSets(3, writes.data(), 0, nullptr);

    // 5. Potok obliczeniowy (Pipeline)
    // ZMIENIONO NAZWĘ PLIKU SHADERA
    auto comp_code = readFile(std::string(EXAMPLE_SOURCE_DIRECTORY) + "/shaders/1plus1.comp.spv");
    VkShaderModule comp_module = createShaderModule(init, comp_code);

    auto const pipeline_layout_info = initializers::PipelineLayoutCreateInfo(descriptor_layouts, {});

    VkPipelineLayout pipeline_layout = nullptr;
    init.disp.createPipelineLayout(&pipeline_layout_info, nullptr, &pipeline_layout);

    VkComputePipelineCreateInfo pipeline_info = {};// NOLINT
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage =
      initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, comp_module, "main");
    pipeline_info.layout = pipeline_layout;

    VkPipeline compute_pipeline = nullptr;
    init.disp.createComputePipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &compute_pipeline);

    // 6. Nagrywanie i wywołanie komendy
    uint32_t const compute_queue_index = init.device.get_queue_index(vkb::QueueType::compute).value();
    auto const cmd_pool_info = initializers::CommandPoolCreateInfo(compute_queue_index);

    VkCommandPool command_pool = nullptr;
    init.disp.createCommandPool(&cmd_pool_info, nullptr, &command_pool);

    auto const cmd_alloc_info = initializers::CommandBufferAllocateInfo(command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);

    VkCommandBuffer command_buffer = nullptr;
    init.disp.allocateCommandBuffers(&cmd_alloc_info, &command_buffer);

    auto const begin_info = initializers::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    init.disp.beginCommandBuffer(command_buffer, &begin_info);
    init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
    init.disp.cmdBindDescriptorSets(
      command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

    // ZMIANA: Shader określa `local_size_x = 64`.
    // Chcemy przetworzyć 1024 elementy, więc odpalamy: 1024 / 64 = 16 grup roboczych.
    init.disp.cmdDispatch(command_buffer, element_count / 64, 1, 1);// NOLINT

    init.disp.endCommandBuffer(command_buffer);

    auto const submit_info = initializers::SubmitInfo({}, {}, std::span{ &command_buffer, 1 }, {});

    init.disp.queueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE);
    init.disp.queueWaitIdle(compute_queue);

    // 7. Odczyt i weryfikacja z bufora wynikowego
    auto output = init.gpu_allocator.read_buffer<float>(*bufferResult, element_count);
    if (!output) {
      std::println("Nie udało się odczytać bufora wynikowego!");
      return;
    }

    std::println("Wyniki dodawania (pierwsze 5 z 1024):");
    for (size_t i{}; i < 5; i++) {// NOLINT
      std::println("Index {}: {} + {} = {}", i, input[i], input[i], output->at(i));// NOLINT
    }

    // 8. Sprzątanie
    init.disp.destroyShaderModule(comp_module, nullptr);
    init.disp.destroyPipeline(compute_pipeline, nullptr);
    init.disp.destroyPipelineLayout(pipeline_layout, nullptr);
    init.disp.destroyDescriptorPool(descriptor_pool, nullptr);
    init.disp.destroyDescriptorSetLayout(descriptor_layout, nullptr);

    init.gpu_allocator.destroy_buffer(*bufferA);
    init.gpu_allocator.destroy_buffer(*bufferB);
    init.gpu_allocator.destroy_buffer(*bufferResult);

    init.disp.destroyCommandPool(command_pool, nullptr);

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

  auto const submit_info = initializers::SubmitInfo(wait_semaphores,
    wait_stages,
    std::span{ &data.command_buffers.at(image_index), 1 },
    signal_semaphores);

  init.disp.resetFences(1, &data.in_flight_fences.at(data.current_frame));

  if (init.disp.queueSubmit(data.graphics_queue, 1, &submit_info, data.in_flight_fences.at(data.current_frame))
      != VK_SUCCESS) {
    std::cout << "failed to submit draw command buffer\n";
    return -1;//"failed to submit draw command buffer
  }

  std::array<VkSwapchainKHR, 1> const swap_chains = { init.swapchain };
  auto const present_info =
    initializers::PresentInfoKHR(signal_semaphores, swap_chains, std::span{ &image_index, 1 });

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

    if (!device_initialization(init).has_value()) { return -1; }

    auto gpu_allocator = vulkan::GPUAllocator::create(init.instance, init.device, init.device.physical_device);
    if (!gpu_allocator) {
      std::println("Nie udało się utworzyć alokatora GPU!");
      return -1;
    }
    init.gpu_allocator = std::move(*gpu_allocator);

    compute::run_compute_test(init);
    if (!create_swapchain(init).has_value()) { return -1; }
    if (!get_queues(init, render_data).has_value()) { return -1; }
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

}// namespace vkgsplat

int main() { return vkgsplat::run(); }
