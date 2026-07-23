#include "backend/vulkan/imgui_overlay.hpp"

#include "app_state.hpp"
#include "backend/vulkan/gpu_allocator.hpp"
#include "backend/vulkan/initializers.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_window/window.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <system_error>
#include <utility>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

struct ImGuiOverlayState
{
  bool initialized = false;
  vulkan::Buffer resource_heap{};
  vulkan::Buffer sampler_heap{};
  void *resource_mapped = nullptr;
  void *sampler_mapped = nullptr;
  VkDeviceSize resource_stride{};
  VkDeviceSize sampler_stride{};
  VkDeviceSize resource_heap_size{};
  VkDeviceSize sampler_heap_size{};
  VkDeviceSize resource_reserved_offset{};
  VkDeviceSize resource_reserved_size{};
  VkDeviceSize sampler_reserved_offset{};
  VkDeviceSize sampler_reserved_size{};
  uint64_t resource_freelist{};
  uint64_t sampler_freelist{};
  VkDevice device{};
  PFN_vkWriteResourceDescriptorsEXT write_resource_descriptors{};
  PFN_vkWriteSamplerDescriptorsEXT write_sampler_descriptors{};
  ImGui_ImplVulkan_DescriptorHeapInfo heap_info{};
  VkPipelineRenderingCreateInfo pipeline_rendering{};
  VkFormat color_format{ VK_FORMAT_UNDEFINED };
};

namespace {

  constexpr uint32_t k_imgui_image_slots = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
  constexpr uint32_t k_imgui_sampler_slots = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE;
  constexpr uint32_t k_freelist_bit_count = 64;
  constexpr float k_fps_window_margin = 12.0F;
  constexpr float k_fps_window_alpha = 0.45F;
  constexpr double k_ms_per_second = 1000.0;

  [[nodiscard]] auto align_buffer_size(VkDeviceSize size, VkDeviceSize alignment) -> VkDeviceSize
  {
    if (alignment == 0) { return size; }
    return (size + alignment - 1) & ~(alignment - 1);
  }

  [[nodiscard]] auto allocate_slot(uint64_t &freelist) -> uint32_t
  {
    for (uint32_t bit_index = 0; bit_index < k_freelist_bit_count; ++bit_index) {
      uint64_t const bit = uint64_t{ 1 } << bit_index;
      if ((freelist & bit) != 0) {
        freelist ^= bit;
        return bit_index;
      }
    }
    return 0;
  }

  void free_slot(uint64_t &freelist, uint32_t index) { freelist |= (uint64_t{ 1 } << index); }

  [[nodiscard]] auto host_descriptor_address(void *mapped, VkDeviceSize stride, uint32_t index) -> void *
  {
    auto const bytes = std::span{ static_cast<std::byte *>(mapped), static_cast<size_t>((index + 1U) * stride) };
    return bytes.subspan(static_cast<size_t>(index * stride)).data();
  }

  auto register_image(void *user_context, VkImageViewCreateInfo const *create_info) -> uint32_t
  {
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    uint32_t const index = allocate_slot(overlay->resource_freelist);

    VkImageDescriptorInfoEXT image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
    image_info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.pView = create_info;

    VkResourceDescriptorInfoEXT resource_info{};
    resource_info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    resource_info.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    resource_info.data.pImage = &image_info;

    VkHostAddressRangeEXT const host_range{
      .address = host_descriptor_address(overlay->resource_mapped, overlay->resource_stride, index),
      .size = overlay->resource_stride,
    };
    overlay->write_resource_descriptors(overlay->device, 1, &resource_info, &host_range);
    return index;
  }

  void unregister_image(void *user_context, uint32_t index)
  {
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    free_slot(overlay->resource_freelist, index);
  }

  auto register_sampler(void *user_context, VkSamplerCreateInfo const *create_info) -> uint32_t
  {
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    uint32_t const index = allocate_slot(overlay->sampler_freelist);

    VkHostAddressRangeEXT const host_range{
      .address = host_descriptor_address(overlay->sampler_mapped, overlay->sampler_stride, index),
      .size = overlay->sampler_stride,
    };
    overlay->write_sampler_descriptors(overlay->device, 1, create_info, &host_range);
    return index;
  }

  void unregister_sampler(void *user_context, uint32_t index)
  {
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    free_slot(overlay->sampler_freelist, index);
  }

  [[nodiscard]] auto load_imgui_vulkan_functions(Init &init) -> bool
  {
    return ImGui_ImplVulkan_LoadFunctions(
      VK_API_VERSION_1_4,
      [](char const *function_name, void *user_data) -> PFN_vkVoidFunction {
        auto *ctx = static_cast<Init *>(user_data);
        // Prefer instance lookup first: ImGui's table includes instance-level
        // entry points, and querying those via vkGetDeviceProcAddr triggers
        // WARNING-vkGetDeviceProcAddr-device.
        if (PFN_vkVoidFunction const instance_fn = vkGetInstanceProcAddr(ctx->instance, function_name);
            instance_fn != nullptr) {
          return instance_fn;
        }
        return vkGetDeviceProcAddr(ctx->device, function_name);
      },
      &init);
  }

  void fill_pipeline_rendering_info(Init const &init, ImGuiOverlayState &overlay)
  {
    overlay.color_format = init.swapchain->format();
    overlay.pipeline_rendering = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .pNext = nullptr,
      .viewMask = 0,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &overlay.color_format,
      .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
    };
  }

  void draw_fps_window()
  {
    ImGuiIO const &imgui_io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(k_fps_window_margin, imgui_io.DisplaySize.y - k_fps_window_margin),
      ImGuiCond_Always,
      ImVec2(0.0F, 1.0F));
    ImGui::SetNextWindowBgAlpha(k_fps_window_alpha);
    // NOLINTBEGIN(hicpp-signed-bitwise)
    ImGuiWindowFlags const flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                                   | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                                   | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    // NOLINTEND(hicpp-signed-bitwise)
    if (ImGui::Begin("FPS", nullptr, flags)) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      ImGui::Text("FPS: %.1f (%.2f ms)",
        static_cast<double>(imgui_io.Framerate),
        k_ms_per_second / static_cast<double>(imgui_io.Framerate));
    }
    ImGui::End();
  }

  void destroy_imgui_heaps(Init &init, ImGuiOverlayState &overlay)
  {
    if (overlay.resource_mapped != nullptr && overlay.resource_heap.handle != VK_NULL_HANDLE) {
      init.gpu_allocator.unmap_buffer(overlay.resource_heap);
      overlay.resource_mapped = nullptr;
    }
    if (overlay.sampler_mapped != nullptr && overlay.sampler_heap.handle != VK_NULL_HANDLE) {
      init.gpu_allocator.unmap_buffer(overlay.sampler_heap);
      overlay.sampler_mapped = nullptr;
    }
    init.gpu_allocator.destroy_buffer(overlay.resource_heap);
    init.gpu_allocator.destroy_buffer(overlay.sampler_heap);
  }

}// namespace

RenderData::RenderData() = default;
RenderData::~RenderData() = default;
RenderData::RenderData(RenderData &&) noexcept = default;
auto RenderData::operator=(RenderData &&) noexcept -> RenderData & = default;

auto init_imgui_overlay(Init &init, RenderData &data) -> std::expected<void, Error>
{
  if (!load_imgui_vulkan_functions(init)) {
    return std::unexpected{ make_error(std::errc::function_not_supported, "failed to load ImGui Vulkan functions") };
  }

  auto overlay = std::make_unique<ImGuiOverlayState>();

  VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
  heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
  VkPhysicalDeviceProperties2 props2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &heap_props,
    .properties = {},
  };
  init.inst_disp.getPhysicalDeviceProperties2(init.device.physical_device, &props2);

  VkDeviceSize const resource_descriptors_size = heap_props.imageDescriptorSize * k_imgui_image_slots;
  VkDeviceSize const sampler_descriptors_size = heap_props.samplerDescriptorSize * k_imgui_sampler_slots;
  VkDeviceSize const resource_size = align_buffer_size(
    resource_descriptors_size + heap_props.minResourceHeapReservedRange, heap_props.resourceHeapAlignment);
  VkDeviceSize const sampler_size = align_buffer_size(
    sampler_descriptors_size + heap_props.minSamplerHeapReservedRange, heap_props.samplerHeapAlignment);

  auto resource_heap = init.gpu_allocator.create_heap_buffer(resource_size);
  if (!resource_heap) {
    return std::unexpected{ make_error(std::errc::not_enough_memory, "failed to create ImGui resource heap") };
  }
  auto sampler_heap = init.gpu_allocator.create_heap_buffer(sampler_size);
  if (!sampler_heap) {
    init.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ make_error(std::errc::not_enough_memory, "failed to create ImGui sampler heap") };
  }

  auto resource_mapped = init.gpu_allocator.map_buffer(*resource_heap);
  if (!resource_mapped) {
    init.gpu_allocator.destroy_buffer(*sampler_heap);
    init.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ make_error(std::errc::io_error, "failed to map ImGui resource heap") };
  }
  auto sampler_mapped = init.gpu_allocator.map_buffer(*sampler_heap);
  if (!sampler_mapped) {
    init.gpu_allocator.unmap_buffer(*resource_heap);
    init.gpu_allocator.destroy_buffer(*sampler_heap);
    init.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ make_error(std::errc::io_error, "failed to map ImGui sampler heap") };
  }

  overlay->resource_heap = *resource_heap;
  overlay->sampler_heap = *sampler_heap;
  overlay->resource_mapped = resource_mapped->data();
  overlay->sampler_mapped = sampler_mapped->data();
  overlay->resource_stride = heap_props.imageDescriptorSize;
  overlay->sampler_stride = heap_props.samplerDescriptorSize;
  overlay->resource_heap_size = resource_size;
  overlay->sampler_heap_size = sampler_size;
  overlay->resource_reserved_offset = resource_descriptors_size;
  overlay->resource_reserved_size = heap_props.minResourceHeapReservedRange;
  overlay->sampler_reserved_offset = sampler_descriptors_size;
  overlay->sampler_reserved_size = heap_props.minSamplerHeapReservedRange;
  overlay->resource_freelist = (uint64_t{ 1 } << k_imgui_image_slots) - uint64_t{ 1 };
  overlay->sampler_freelist = (uint64_t{ 1 } << k_imgui_sampler_slots) - uint64_t{ 1 };
  overlay->device = init.device;
  overlay->write_resource_descriptors = init.write_resource_descriptors;
  overlay->write_sampler_descriptors = init.write_sampler_descriptors;
  overlay->heap_info = {
    .RegisterSampler = register_sampler,
    .UnRegisterSampler = unregister_sampler,
    .RegisterImage = register_image,
    .UnRegisterImage = unregister_image,
    .UserContext = overlay.get(),
  };

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  auto *glfw_window = static_cast<GLFWwindow *>(init.window->native_handle());
  if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
    destroy_imgui_heaps(init, *overlay);
    ImGui::DestroyContext();
    return std::unexpected{ make_error(std::errc::io_error, "failed to initialize ImGui GLFW backend") };
  }

  fill_pipeline_rendering_info(init, *overlay);

  ImGui_ImplVulkan_InitInfo vulkan_init{};
  vulkan_init.ApiVersion = VK_API_VERSION_1_4;
  vulkan_init.Instance = init.instance;
  vulkan_init.PhysicalDevice = init.device.physical_device;
  vulkan_init.Device = init.device;
  vulkan_init.QueueFamily = init.device.get_queue_index(vkb::QueueType::graphics).value();
  vulkan_init.Queue = data.graphics_queue;
  vulkan_init.MinImageCount = static_cast<uint32_t>(init.swapchain->image_count());
  vulkan_init.ImageCount = static_cast<uint32_t>(init.swapchain->image_count());
  vulkan_init.UseDynamicRendering = true;
  vulkan_init.PipelineInfoMain.PipelineRenderingCreateInfo = overlay->pipeline_rendering;
  vulkan_init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  vulkan_init.DescriptorHeapInfo = &overlay->heap_info;

  if (!ImGui_ImplVulkan_Init(&vulkan_init)) {
    ImGui_ImplGlfw_Shutdown();
    destroy_imgui_heaps(init, *overlay);
    ImGui::DestroyContext();
    return std::unexpected{ make_error(std::errc::io_error, "failed to initialize ImGui Vulkan backend") };
  }

  overlay->initialized = true;
  data.imgui = std::move(overlay);
  return {};
}

void shutdown_imgui_overlay(Init &init, RenderData &data)
{
  if (data.imgui == nullptr) { return; }

  if (data.imgui->initialized) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    data.imgui->initialized = false;
  }

  destroy_imgui_heaps(init, *data.imgui);
  data.imgui.reset();
}

void recreate_imgui_overlay_pipeline(Init &init, RenderData &data)
{
  if (data.imgui == nullptr || !data.imgui->initialized) { return; }

  fill_pipeline_rendering_info(init, *data.imgui);
  ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(init.swapchain->image_count()));

  ImGui_ImplVulkan_PipelineInfo pipeline_info{};
  pipeline_info.PipelineRenderingCreateInfo = data.imgui->pipeline_rendering;
  pipeline_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_CreateMainPipeline(&pipeline_info);
}

void record_imgui_overlay(Init &init, RenderData const &data, VkCommandBuffer command_buffer, size_t image_index)
{
  if (data.imgui == nullptr || !data.imgui->initialized) { return; }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  draw_fps_window();
  ImGui::Render();

  VkImage swapchain_image = init.swapchain->images().at(image_index);
  VkImageView swapchain_view = init.swapchain->image_views().at(image_index);
  VkExtent2D const extent = init.swapchain->extent();

  VkImageSubresourceRange const color_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  auto to_color_attachment = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, swapchain_image, color_range);
  to_color_attachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_color_attachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &to_color_attachment);

  VkRenderingAttachmentInfo const color_attachment = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .pNext = nullptr,
    .imageView = swapchain_view,
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .resolveMode = VK_RESOLVE_MODE_NONE,
    .resolveImageView = VK_NULL_HANDLE,
    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = {},
  };

  VkRenderingInfo const rendering_info = {
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .pNext = nullptr,
    .flags = 0,
    .renderArea = { .offset = { .x = 0, .y = 0 }, .extent = extent },
    .layerCount = 1,
    .viewMask = 0,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment,
    .pDepthAttachment = nullptr,
    .pStencilAttachment = nullptr,
  };

  init.disp.cmdBeginRendering(command_buffer, &rendering_info);

  VkBindHeapInfoEXT const resource_bind = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange =
      {
        .address = init.gpu_allocator.get_buffer_device_address(data.imgui->resource_heap),
        .size = data.imgui->resource_heap_size,
      },
    .reservedRangeOffset = data.imgui->resource_reserved_offset,
    .reservedRangeSize = data.imgui->resource_reserved_size,
  };
  VkBindHeapInfoEXT const sampler_bind = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange =
      {
        .address = init.gpu_allocator.get_buffer_device_address(data.imgui->sampler_heap),
        .size = data.imgui->sampler_heap_size,
      },
    .reservedRangeOffset = data.imgui->sampler_reserved_offset,
    .reservedRangeSize = data.imgui->sampler_reserved_size,
  };
  init.cmd_bind_resource_heap(command_buffer, &resource_bind);
  init.cmd_bind_sampler_heap(command_buffer, &sampler_bind);

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
  init.disp.cmdEndRendering(command_buffer);

  auto to_present = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, swapchain_image, color_range);
  to_present.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &to_present);
}

}// namespace vkgsplat
