#include "backend/vulkan/imgui_overlay.hpp"

#include "app_state.hpp"
#include "backend/vulkan/gpu_allocator.hpp"
#include "backend/vulkan/gpu_pass_timer.hpp"
#include "backend/vulkan/initializers.hpp"
#include "frame_context.hpp"
#include "vulkan_context.hpp"
#include <vkgsplat/platform.hpp>
#include <vkgsplat_utility/error.hpp>
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <memory>
#include <print>
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
  VkBindHeapInfoEXT resource_bind{};
  VkBindHeapInfoEXT sampler_bind{};
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
  vulkan::GPUAllocator *gpu_allocator{};
  PFN_vkWriteResourceDescriptorsEXT write_resource_descriptors{};
  PFN_vkWriteSamplerDescriptorsEXT write_sampler_descriptors{};
  ImGui_ImplVulkan_DescriptorHeapInfo heap_info{};
  VkPipelineRenderingCreateInfo pipeline_rendering{};
  VkFormat color_format{ VK_FORMAT_UNDEFINED };

  // Rebuild ImGui draw data infrequently; reuse per-frame secondary CBs between updates.
  std::array<VkCommandBuffer, kMaxFramesInFlight> overlay_secondaries{};
  std::array<uint64_t, kMaxFramesInFlight> secondary_generation{};
  std::array<ImDrawData *, kMaxFramesInFlight> frame_draw_data{};
  uint64_t ui_generation = 1;
  std::array<char, kImGuiFpsLabelCapacity> fps_label{ "FPS: --" };
  std::array<char, kImGuiGpuLabelCapacity> gpu_label{};
  std::chrono::steady_clock::time_point last_fps_update;
  std::chrono::steady_clock::time_point last_frame_time;
  float fps_ema = 0.0F;
  VkExtent2D last_extent{};
};

namespace {

  constexpr uint32_t kImguiImageSlots = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
  constexpr uint32_t kImguiSamplerSlots = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE;
  constexpr uint32_t kFreelistBitCount = 64;
  constexpr float kFpsWindowMargin = 12.0F;
  constexpr float kFpsWindowAlpha = 0.45F;
  constexpr double kMsPerSecond = 1000.0;
  constexpr auto kFpsUpdateInterval = std::chrono::milliseconds{ 100 };
  constexpr float kFpsEmaAlpha = 0.1F;

  [[nodiscard]] auto AlignBufferSize(VkDeviceSize size, VkDeviceSize alignment) -> VkDeviceSize
  {
    if (alignment == 0) { return size; }
    return (size + alignment - 1) & ~(alignment - 1);
  }

  [[nodiscard]] auto AllocateSlot(uint64_t &freelist) -> uint32_t
  {
    for (uint32_t bit_index = 0; bit_index < kFreelistBitCount; ++bit_index) {
      uint64_t const bit = uint64_t{ 1 } << bit_index;
      if ((freelist & bit) != 0) {
        freelist ^= bit;
        return bit_index;
      }
    }
    return 0;
  }

  void FreeSlot(uint64_t &freelist, uint32_t index) { freelist |= (uint64_t{ 1 } << index); }

  [[nodiscard]] auto HostDescriptorAddress(void *mapped, VkDeviceSize stride, uint32_t index) -> void *
  {
    auto const bytes = std::span{ static_cast<std::byte *>(mapped), static_cast<size_t>((index + 1U) * stride) };
    return bytes.subspan(static_cast<size_t>(index * stride)).data();
  }

  auto RegisterImage(void *user_context, VkImageViewCreateInfo const *create_info) -> uint32_t
  {
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    uint32_t const index = AllocateSlot(overlay->resource_freelist);
    // ImTextureID 0 is ImTextureID_Invalid; descriptor-heap mode requires non-zero RegisterImage indices.
    if (index == 0) {
      std::println(stderr, "[imgui] RegisterImage: no free non-zero heap slots");
      return 0;
    }

    VkImageDescriptorInfoEXT image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT;
    image_info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.pView = create_info;

    VkResourceDescriptorInfoEXT resource_info{};
    resource_info.sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT;
    resource_info.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    resource_info.data.pImage = &image_info;

    VkHostAddressRangeEXT const host_range{
      .address = HostDescriptorAddress(overlay->resource_mapped, overlay->resource_stride, index),
      .size = overlay->resource_stride,
    };
    if (overlay->write_resource_descriptors(overlay->device, 1, &resource_info, &host_range) != VK_SUCCESS) {
      std::println(stderr, "[imgui] vkWriteResourceDescriptorsEXT failed for image slot {}", index);
      FreeSlot(overlay->resource_freelist, index);
      return 0;
    }
    if (overlay->gpu_allocator != nullptr) { overlay->gpu_allocator->flush_buffer(overlay->resource_heap); }
    return index;
  }

  void UnregisterImage(void *user_context, uint32_t index)
  {
    if (index == 0) { return; }
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    FreeSlot(overlay->resource_freelist, index);
  }

  auto RegisterSampler(void *user_context, VkSamplerCreateInfo const *create_info) -> uint32_t
  {
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    uint32_t const index = AllocateSlot(overlay->sampler_freelist);

    VkHostAddressRangeEXT const host_range{
      .address = HostDescriptorAddress(overlay->sampler_mapped, overlay->sampler_stride, index),
      .size = overlay->sampler_stride,
    };
    if (overlay->write_sampler_descriptors(overlay->device, 1, create_info, &host_range) != VK_SUCCESS) {
      std::println(stderr, "[imgui] vkWriteSamplerDescriptorsEXT failed for sampler slot {}", index);
    }
    if (overlay->gpu_allocator != nullptr) { overlay->gpu_allocator->flush_buffer(overlay->sampler_heap); }
    return index;
  }

  void UnregisterSampler(void *user_context, uint32_t index)
  {
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    FreeSlot(overlay->sampler_freelist, index);
  }

  [[nodiscard]] auto IsInstanceProcName(char const *function_name) -> bool
  {
    // Avoid WARNING-vkGetDeviceProcAddr-device for instance-level entry points
    // while still resolving device extensions (e.g. vkCmdPushDataEXT) via the device.
    return std::strstr(function_name, "PhysicalDevice") != nullptr || std::strstr(function_name, "Surface") != nullptr
           || std::strcmp(function_name, "vkCreateInstance") == 0
           || std::strcmp(function_name, "vkDestroyInstance") == 0
           || std::strcmp(function_name, "vkEnumerateInstanceExtensionProperties") == 0
           || std::strcmp(function_name, "vkEnumerateInstanceLayerProperties") == 0
           || std::strcmp(function_name, "vkEnumerateInstanceVersion") == 0
           || std::strcmp(function_name, "vkGetInstanceProcAddr") == 0
           || std::strstr(function_name, "DebugReport") != nullptr
           || std::strstr(function_name, "DebugUtils") != nullptr;
  }

  [[nodiscard]] auto LoadImguiVulkanFunctions(Init &init) -> bool
  {
    return ImGui_ImplVulkan_LoadFunctions(
      VK_API_VERSION_1_4,
      [](char const *function_name, void *user_data) -> PFN_vkVoidFunction {
        auto *ctx = static_cast<Init *>(user_data);
        if (IsInstanceProcName(function_name)) { return vkGetInstanceProcAddr(ctx->instance, function_name); }
        if (PFN_vkVoidFunction const device_fn = vkGetDeviceProcAddr(ctx->device, function_name);
          device_fn != nullptr) {
          return device_fn;
        }
        return vkGetInstanceProcAddr(ctx->instance, function_name);
      },
      &init);
  }

  void CheckImguiVkResult(VkResult result)
  {
    if (result == VK_SUCCESS) { return; }
    std::println(stderr, "[imgui] Vulkan error: VkResult={}", static_cast<int>(result));
  }

  void FillPipelineRenderingInfo(Init const &init, ImGuiOverlayState &overlay)
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

  void DrawFpsWindow(char const *fps_label, char const *gpu_label)
  {
    ImGuiIO const &imgui_io = ImGui::GetIO();
    ImGui::SetNextWindowPos(
      ImVec2(kFpsWindowMargin, imgui_io.DisplaySize.y - kFpsWindowMargin), ImGuiCond_Always, ImVec2(0.0F, 1.0F));
    ImGui::SetNextWindowBgAlpha(kFpsWindowAlpha);
    // NOLINTBEGIN(hicpp-signed-bitwise)
    ImGuiWindowFlags const flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                                   | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                                   | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    // NOLINTEND(hicpp-signed-bitwise)
    if (ImGui::Begin("FPS", nullptr, flags)) {
      ImGui::TextUnformatted(fps_label);
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      if (gpu_label != nullptr && gpu_label[0] != '\0') { ImGui::TextUnformatted(gpu_label); }
    }
    ImGui::End();
  }

  void InvalidateOverlaySecondaries(ImGuiOverlayState &overlay)
  {
    ++overlay.ui_generation;
    overlay.secondary_generation.fill(0);
    overlay.frame_draw_data.fill(nullptr);
  }

  [[nodiscard]] auto AllocateOverlaySecondaries(RenderData &data) -> bool
  {
    if (data.imgui == nullptr || !data.command_pool) { return false; }

    auto buffers =
      data.command_pool->allocate_buffers(static_cast<u32>(kMaxFramesInFlight), VK_COMMAND_BUFFER_LEVEL_SECONDARY);
    if (!buffers) { return false; }

    for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
      data.imgui->overlay_secondaries.at(i) = buffers->at(i).handle();
      data.imgui->secondary_generation.at(i) = 0;
    }
    return true;
  }

  void SampleFrameTime(ImGuiOverlayState &overlay)
  {
    auto const now = std::chrono::steady_clock::now();
    if (overlay.last_frame_time.time_since_epoch().count() != 0) {
      float const frame_delta = std::chrono::duration<float>(now - overlay.last_frame_time).count();
      if (frame_delta > 0.0F) {
        float const fps = 1.0F / frame_delta;
        overlay.fps_ema =
          overlay.fps_ema > 0.0F ? ((1.0F - kFpsEmaAlpha) * overlay.fps_ema) + (kFpsEmaAlpha * fps) : fps;
      }
    }
    overlay.last_frame_time = now;

    bool const first_sample = overlay.last_fps_update.time_since_epoch().count() == 0;
    if (first_sample || now - overlay.last_fps_update >= kFpsUpdateInterval) {
      overlay.last_fps_update = now;
      float const fps = overlay.fps_ema > 0.0F ? overlay.fps_ema : 0.0F;
      double const frame_ms = fps > 0.0F ? kMsPerSecond / static_cast<double>(fps) : 0.0;
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      (void)std::snprintf(
        overlay.fps_label.data(), overlay.fps_label.size(), "FPS: %.1f (%.2f ms)", static_cast<double>(fps), frame_ms);
      InvalidateOverlaySecondaries(overlay);
    }
  }

  void RecordOverlaySecondary(Init &init, RenderData const &data, size_t frame_slot)
  {
    auto &overlay = *data.imgui;
    VkCommandBuffer secondary = overlay.overlay_secondaries.at(frame_slot);

    VkCommandBufferInheritanceRenderingInfo inheritance_rendering{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
      .pNext = nullptr,
      .flags = 0,
      .viewMask = 0,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &overlay.color_format,
      .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkCommandBufferInheritanceDescriptorHeapInfoEXT inheritance_heap{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_DESCRIPTOR_HEAP_INFO_EXT,
      .pNext = &inheritance_rendering,
      .pSamplerHeapBindInfo = &overlay.sampler_bind,
      .pResourceHeapBindInfo = &overlay.resource_bind,
    };

    VkCommandBufferInheritanceInfo const inheritance{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
      .pNext = &inheritance_heap,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
      .framebuffer = VK_NULL_HANDLE,
      .occlusionQueryEnable = VK_FALSE,
      .queryFlags = 0,
      .pipelineStatistics = 0,
    };

    VkCommandBufferBeginInfo const begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
      .pInheritanceInfo = &inheritance,
    };

    init.disp.resetCommandBuffer(secondary, 0);
    init.disp.beginCommandBuffer(secondary, &begin_info);

    ImDrawData *draw_data = overlay.frame_draw_data.at(frame_slot);
    if (draw_data != nullptr) {
      ImGui_ImplVulkan_RenderDrawData(draw_data, secondary);
    }

    init.disp.endCommandBuffer(secondary);
    overlay.secondary_generation.at(frame_slot) = FrameSetupFor(data, frame_slot).imgui.ui_generation;
  }

  void DestroyImguiHeaps(Init &init, ImGuiOverlayState &overlay)
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

void UpdateImguiGpuTimings(RenderData &data)
{
  if (data.imgui == nullptr || !data.gpu_pass_timer.enabled()) { return; }

  static auto last_update = std::chrono::steady_clock::time_point{};
  auto const now = std::chrono::steady_clock::now();
  constexpr auto kGpuLabelUpdateInterval = std::chrono::milliseconds{ 100 };
  if (last_update.time_since_epoch().count() != 0 && now - last_update < kGpuLabelUpdateInterval) { return; }
  last_update = now;

  auto const &pass_ms = data.gpu_pass_timer.last_ms();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  (void)std::snprintf(data.imgui->gpu_label.data(),
    data.imgui->gpu_label.size(),
    "GPU: %.2f ms\n  %s %.2f  %s %.2f  %s %.2f\n  %s %.2f  %s %.2f",
    static_cast<double>(data.gpu_pass_timer.total_ms()),
    GpuPassName(GpuPass::kProjection),
    static_cast<double>(pass_ms.at(static_cast<size_t>(GpuPass::kProjection))),
    GpuPassName(GpuPass::kBinning),
    static_cast<double>(pass_ms.at(static_cast<size_t>(GpuPass::kBinning))),
    GpuPassName(GpuPass::kPrepareSort),
    static_cast<double>(pass_ms.at(static_cast<size_t>(GpuPass::kPrepareSort))),
    GpuPassName(GpuPass::kRadixSort),
    static_cast<double>(pass_ms.at(static_cast<size_t>(GpuPass::kRadixSort))),
    GpuPassName(GpuPass::kRasterize),
    static_cast<double>(pass_ms.at(static_cast<size_t>(GpuPass::kRasterize))));
  ++data.imgui->ui_generation;
  data.imgui->secondary_generation.fill(0);
}

auto InitImguiOverlay(Init &init, RenderData &data) -> std::expected<void, Error>
{
  if (!LoadImguiVulkanFunctions(init)) {
    return std::unexpected{ MakeError(std::errc::function_not_supported, "failed to load ImGui Vulkan functions") };
  }

  auto overlay = std::make_unique<ImGuiOverlayState>();
  overlay->device = init.device;
  overlay->gpu_allocator = &init.gpu_allocator;
  overlay->write_resource_descriptors = init.write_resource_descriptors;
  overlay->write_sampler_descriptors = init.write_sampler_descriptors;

  VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
  heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
  VkPhysicalDeviceProperties2 props2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &heap_props,
    .properties = {},
  };
  init.inst_disp.getPhysicalDeviceProperties2(init.device.physical_device, &props2);

  VkDeviceSize const resource_descriptors_size = heap_props.imageDescriptorSize * kImguiImageSlots;
  VkDeviceSize const sampler_descriptors_size = heap_props.samplerDescriptorSize * kImguiSamplerSlots;
  VkDeviceSize const resource_size = AlignBufferSize(
    resource_descriptors_size + heap_props.minResourceHeapReservedRange, heap_props.resourceHeapAlignment);
  VkDeviceSize const sampler_size =
    AlignBufferSize(sampler_descriptors_size + heap_props.minSamplerHeapReservedRange, heap_props.samplerHeapAlignment);

  auto resource_heap = init.gpu_allocator.create_heap_buffer(resource_size);
  if (!resource_heap) {
    return std::unexpected{ MakeError(std::errc::not_enough_memory, "failed to create ImGui resource heap") };
  }
  auto sampler_heap = init.gpu_allocator.create_heap_buffer(sampler_size);
  if (!sampler_heap) {
    init.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ MakeError(std::errc::not_enough_memory, "failed to create ImGui sampler heap") };
  }

  auto resource_mapped = init.gpu_allocator.map_buffer(*resource_heap);
  if (!resource_mapped) {
    init.gpu_allocator.destroy_buffer(*sampler_heap);
    init.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ MakeError(std::errc::io_error, "failed to map ImGui resource heap") };
  }
  auto sampler_mapped = init.gpu_allocator.map_buffer(*sampler_heap);
  if (!sampler_mapped) {
    init.gpu_allocator.unmap_buffer(*resource_heap);
    init.gpu_allocator.destroy_buffer(*sampler_heap);
    init.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ MakeError(std::errc::io_error, "failed to map ImGui sampler heap") };
  }

  overlay->resource_heap = *resource_heap;
  overlay->sampler_heap = *sampler_heap;
  overlay->resource_mapped = resource_mapped->data();
  overlay->sampler_mapped = sampler_mapped->data();
  overlay->resource_stride = heap_props.imageDescriptorSize;
  overlay->sampler_stride = heap_props.samplerDescriptorSize;
  overlay->resource_heap_size = resource_descriptors_size + heap_props.minResourceHeapReservedRange;
  overlay->sampler_heap_size = sampler_descriptors_size + heap_props.minSamplerHeapReservedRange;
  overlay->resource_reserved_offset = resource_descriptors_size;
  overlay->resource_reserved_size = heap_props.minResourceHeapReservedRange;
  overlay->sampler_reserved_offset = sampler_descriptors_size;
  overlay->sampler_reserved_size = heap_props.minSamplerHeapReservedRange;
  overlay->resource_freelist = ((uint64_t{ 1 } << kImguiImageSlots) - uint64_t{ 1 }) & ~uint64_t{ 1 };// slot 0 reserved
  overlay->sampler_freelist = (uint64_t{ 1 } << kImguiSamplerSlots) - uint64_t{ 1 };
  overlay->heap_info = {
    .RegisterImage = RegisterImage,
    .UnRegisterImage = UnregisterImage,
    .RegisterSampler = RegisterSampler,
    .UnRegisterSampler = UnregisterSampler,
    .UserContext = overlay.get(),
  };

  overlay->resource_bind = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = {
      .address = init.gpu_allocator.get_buffer_device_address(*resource_heap),
      .size = overlay->resource_heap_size,
    },
    .reservedRangeOffset = overlay->resource_reserved_offset,
    .reservedRangeSize = overlay->resource_reserved_size,
  };

  overlay->sampler_bind = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = {
      .address = init.gpu_allocator.get_buffer_device_address(*sampler_heap),
      .size = overlay->sampler_heap_size,
    },
    .reservedRangeOffset = overlay->sampler_reserved_offset,
    .reservedRangeSize = overlay->sampler_reserved_size,
  };


  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  auto *glfw_window = static_cast<GLFWwindow *>(init.platform->native_window());
  if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
    DestroyImguiHeaps(init, *overlay);
    ImGui::DestroyContext();
    return std::unexpected{ MakeError(std::errc::io_error, "failed to initialize ImGui GLFW backend") };
  }

  FillPipelineRenderingInfo(init, *overlay);

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
  vulkan_init.CheckVkResultFn = CheckImguiVkResult;
  vulkan_init.DescriptorHeapInfo = &overlay->heap_info;

  if (!ImGui_ImplVulkan_Init(&vulkan_init)) {
    ImGui_ImplGlfw_Shutdown();
    DestroyImguiHeaps(init, *overlay);
    ImGui::DestroyContext();
    return std::unexpected{ MakeError(std::errc::io_error, "failed to initialize ImGui Vulkan backend") };
  }

  overlay->initialized = true;
  data.imgui = std::move(overlay);

  if (!AllocateOverlaySecondaries(data)) {
    ShutdownImguiOverlay(init, data);
    return std::unexpected{ MakeError(std::errc::io_error, "failed to allocate ImGui overlay command buffers") };
  }

  return {};
}

void ShutdownImguiOverlay(Init &init, RenderData &data)
{
  if (data.imgui == nullptr) { return; }

  if (data.imgui->initialized) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    data.imgui->initialized = false;
  }

  DestroyImguiHeaps(init, *data.imgui);
  data.imgui.reset();
}

void RecreateImguiOverlayPipeline(Init &init, RenderData &data)
{
  if (data.imgui == nullptr || !data.imgui->initialized) { return; }

  FillPipelineRenderingInfo(init, *data.imgui);
  ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(init.swapchain->image_count()));

  ImGui_ImplVulkan_PipelineInfo pipeline_info{};
  pipeline_info.PipelineRenderingCreateInfo = data.imgui->pipeline_rendering;
  pipeline_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_CreateMainPipeline(&pipeline_info);

  // Command pool was recreated with the swapchain; reclaim secondary CBs.
  if (!AllocateOverlaySecondaries(data)) {
    std::println(stderr, "[imgui] failed to reallocate overlay command buffers after swapchain recreate");
  }
  InvalidateOverlaySecondaries(*data.imgui);
  data.imgui->frame_draw_data.fill(nullptr);
  data.imgui->last_extent = {};
}

void BuildImGuiFrameSnapshot(RenderData &data, size_t frame_slot, ImGuiFrameSnapshot &out_snapshot)
{
  out_snapshot = {};

  if (data.imgui == nullptr || !data.imgui->initialized) { return; }

  auto &overlay = *data.imgui;
  SampleFrameTime(overlay);

  out_snapshot.ui_generation = overlay.ui_generation;
  std::memcpy(out_snapshot.fps_label.data(), overlay.fps_label.data(), overlay.fps_label.size());
  std::memcpy(out_snapshot.gpu_label.data(), overlay.gpu_label.data(), overlay.gpu_label.size());

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  DrawFpsWindow(out_snapshot.fps_label.data(), out_snapshot.gpu_label.data());
  ImGui::Render();
  overlay.frame_draw_data.at(frame_slot) = ImGui::GetDrawData();
}

void RecordImguiOverlay(Init &init, RenderData const &data, VkCommandBuffer command_buffer, size_t image_index)
{
  VkImage swapchain_image = init.swapchain->images().at(image_index);
  VkImageSubresourceRange const color_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  // Raster blits into TRANSFER_DST; always finish with PRESENT.
  if (data.imgui == nullptr || !data.imgui->initialized) {
    auto to_present = initializers::ImageMemoryBarrier(
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, swapchain_image, color_range);
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    init.disp.cmdPipelineBarrier(command_buffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &to_present);
    return;
  }

  auto &overlay = *data.imgui;

  VkExtent2D const extent = init.swapchain->vk_extent();
  if (extent.width != overlay.last_extent.width || extent.height != overlay.last_extent.height) {
    overlay.last_extent = extent;
    InvalidateOverlaySecondaries(overlay);
  }

  size_t const frame_slot = data.current_frame;
  if (overlay.overlay_secondaries.at(frame_slot) == VK_NULL_HANDLE) {
    auto to_present = initializers::ImageMemoryBarrier(
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, swapchain_image, color_range);
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    init.disp.cmdPipelineBarrier(command_buffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &to_present);
    return;
  }

  if (overlay.secondary_generation.at(frame_slot) != FrameSetupFor(data, frame_slot).imgui.ui_generation) {
    RecordOverlaySecondary(init, data, frame_slot);
  }

  init.cmd_bind_resource_heap(command_buffer, &overlay.resource_bind);
  init.cmd_bind_sampler_heap(command_buffer, &overlay.sampler_bind);

  VkImageView swapchain_view = init.swapchain->image_views().at(image_index);

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
    .flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,
    .renderArea = { .offset = { .x = 0, .y = 0 }, .extent = extent },
    .layerCount = 1,
    .viewMask = 0,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment,
    .pDepthAttachment = nullptr,
    .pStencilAttachment = nullptr,
  };

  init.disp.cmdBeginRendering(command_buffer, &rendering_info);
  init.disp.cmdExecuteCommands(command_buffer, 1, &overlay.overlay_secondaries.at(frame_slot));
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
