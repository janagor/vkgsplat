#include "vulkan/imgui_overlay.hpp"

#include "app_state.hpp"
#include "vulkan/gpu_allocator.hpp"
#include "vulkan/gpu_pass_timer.hpp"
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

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <vkexec/barrier.hpp>
#include <vkexec_extensions/descriptor_heap/descriptor_heap.hpp>
#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

namespace vkgsplat {

struct ImGuiOverlayState
{
  bool initialized = false;
  vulkan::Buffer resource_heap;
  vulkan::Buffer sampler_heap;
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
  vkexec::context *vkexec_context{};
  vulkan::GPUAllocator *gpu_allocator{};
  ImGui_ImplVulkan_DescriptorHeapInfo heap_info{};
  VkPipelineRenderingCreateInfo pipeline_rendering{};
  VkFormat color_format{ VK_FORMAT_UNDEFINED };

  // Rebuild ImGui draw data infrequently; reuse per-frame secondary CBs between updates.
  // frame_draw_data owns per-slot clones — ImGui::GetDrawData() is invalidated by the next
  // NewFrame() and must not be read from the render thread under pipelined SubmitFrame.
  std::array<VkCommandBuffer, kFrameSlotCount> overlay_secondaries{};
  std::array<uint64_t, kFrameSlotCount> secondary_generation{};
  std::array<ImDrawData *, kFrameSlotCount> frame_draw_data{};
  uint64_t ui_generation = 1;
  std::array<char, kImGuiFpsLabelCapacity> fps_label{ "FPS: --" };
  std::array<char, kImGuiGpuLabelCapacity> gpu_label{};
  std::chrono::steady_clock::time_point last_fps_update;
  std::chrono::steady_clock::time_point last_frame_time;
  float fps_ema = 0.0F;
  // After an idle gap, skip the next interval(s) while submit cadence restabilizes.
  u32 fps_ignore_samples = 0;
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
  // Accept only plausible inter-submit periods (reject idle gaps and near-instant refill submits).
  constexpr float kMinFrameDeltaForFps = 1.0F / 240.0F;
  constexpr float kMaxFrameDeltaForFps = 1.0F / 15.0F;
  constexpr u32 kFpsIgnoreSamplesAfterHitch = 2;

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

  [[nodiscard]] auto HostDescriptorSpan(void *mapped, VkDeviceSize stride, uint32_t index) -> std::span<std::byte>
  {
    auto const bytes = std::span{ static_cast<std::byte *>(mapped), static_cast<size_t>((index + 1U) * stride) };
    return bytes.subspan(static_cast<size_t>(index * stride), static_cast<size_t>(stride));
  }

  auto RegisterImage(void *user_context, VkImageViewCreateInfo const *create_info) -> uint32_t
  {
    auto *overlay = static_cast<ImGuiOverlayState *>(user_context);
    if (overlay->vkexec_context == nullptr || create_info == nullptr) { return 0; }

    uint32_t const index = AllocateSlot(overlay->resource_freelist);
    // ImTextureID 0 is ImTextureID_Invalid; descriptor-heap mode requires non-zero RegisterImage indices.
    if (index == 0) {
      std::println(stderr, "[imgui] RegisterImage: no free non-zero heap slots");
      return 0;
    }

    auto const destination = HostDescriptorSpan(overlay->resource_mapped, overlay->resource_stride, index);
    auto const wrote = vkexec::write_sampled_image_descriptor(
      *overlay->vkexec_context, *create_info, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, destination);
    if (!wrote) {
      std::println(stderr, "[imgui] write_sampled_image_descriptor failed for image slot {}: {}",
        index,
        wrote.error().message());
      FreeSlot(overlay->resource_freelist, index);
      return 0;
    }
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
    if (overlay->vkexec_context == nullptr || create_info == nullptr) { return 0; }

    uint32_t const index = AllocateSlot(overlay->sampler_freelist);
    auto const destination = HostDescriptorSpan(overlay->sampler_mapped, overlay->sampler_stride, index);
    auto const wrote = vkexec::write_sampler_descriptor(*overlay->vkexec_context, *create_info, destination);
    if (!wrote) {
      std::println(stderr,
        "[imgui] write_sampler_descriptor failed for sampler slot {}: {}",
        index,
        wrote.error().message());
      FreeSlot(overlay->sampler_freelist, index);
      return 0;
    }
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

  [[nodiscard]] auto LoadImguiVulkanFunctions(vulkan::Context &context) -> bool
  {
    return ImGui_ImplVulkan_LoadFunctions(
      VK_API_VERSION_1_4,
      [](char const *function_name, void *user_data) -> PFN_vkVoidFunction {
        auto *ctx = static_cast<vulkan::Context *>(user_data);
        if (IsInstanceProcName(function_name)) { return vkGetInstanceProcAddr(ctx->instance, function_name); }
        if (PFN_vkVoidFunction const device_fn = vkGetDeviceProcAddr(ctx->device, function_name);
          device_fn != nullptr) {
          return device_fn;
        }
        return vkGetInstanceProcAddr(ctx->instance, function_name);
      },
      &context);
  }

  void CheckImguiVkResult(VkResult result)
  {
    if (result == VK_SUCCESS) { return; }
    std::println(stderr, "[imgui] Vulkan error: VkResult={}", static_cast<int>(result));
  }

  void FillPipelineRenderingInfo(vulkan::Context const &context, ImGuiOverlayState &overlay)
  {
    overlay.color_format = context.swapchain->format();
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

  void DestroyClonedDrawData(ImDrawData *draw_data)
  {
    if (draw_data == nullptr) { return; }
    for (ImDrawList *cmd_list : draw_data->CmdLists) { IM_DELETE(cmd_list); }
    draw_data->CmdLists.clear();
    IM_DELETE(draw_data);
  }

  void ClearClonedDrawData(ImGuiOverlayState &overlay)
  {
    for (ImDrawData *&draw_data : overlay.frame_draw_data) {
      DestroyClonedDrawData(draw_data);
      draw_data = nullptr;
    }
  }

  [[nodiscard]] auto CloneImDrawData(ImDrawData const *src) -> ImDrawData *
  {
    if (src == nullptr || !src->Valid) { return nullptr; }

    // ImGui heap ownership; freed via DestroyClonedDrawData / IM_DELETE.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto *dst = IM_NEW(ImDrawData)();
    dst->Valid = true;
    dst->FrameCount = src->FrameCount;
    dst->DisplayPos = src->DisplayPos;
    dst->DisplaySize = src->DisplaySize;
    dst->FramebufferScale = src->FramebufferScale;
    dst->OwnerViewport = nullptr;
    dst->Textures = src->Textures;
    for (ImDrawList const *cmd_list : src->CmdLists) { dst->AddDrawList(cmd_list->CloneOutput()); }
    return dst;
  }

  void InvalidateOverlaySecondaries(ImGuiOverlayState &overlay)
  {
    ++overlay.ui_generation;
    overlay.secondary_generation.fill(0);
  }

  [[nodiscard]] auto AllocateOverlaySecondaries(RenderData &data) -> bool
  {
    if (data.imgui == nullptr || !data.command_pool) { return false; }

    auto buffers =
      data.command_pool->allocate_buffers(static_cast<u32>(kFrameSlotCount), VK_COMMAND_BUFFER_LEVEL_SECONDARY);
    if (!buffers) { return false; }

    for (size_t i = 0; i < kFrameSlotCount; ++i) {
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

      if (overlay.fps_ignore_samples > 0U) {
        --overlay.fps_ignore_samples;
      } else if (frame_delta >= kMinFrameDeltaForFps && frame_delta <= kMaxFrameDeltaForFps) {
        float const fps = 1.0F / frame_delta;
        overlay.fps_ema =
          overlay.fps_ema > 0.0F ? ((1.0F - kFpsEmaAlpha) * overlay.fps_ema) + (kFpsEmaAlpha * fps) : fps;
      } else if (frame_delta > kMaxFrameDeltaForFps) {
        // Idle wait or hitch: don't poison EMA, and ignore the next refill intervals.
        overlay.fps_ignore_samples = kFpsIgnoreSamplesAfterHitch;
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

  void RecordOverlaySecondary(vulkan::Context &context, RenderData const &data, size_t frame_slot)
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

    context.disp.resetCommandBuffer(secondary, 0);
    context.disp.beginCommandBuffer(secondary, &begin_info);

    ImDrawData *draw_data = overlay.frame_draw_data.at(frame_slot);
    if (draw_data != nullptr) {
      ImGui_ImplVulkan_RenderDrawData(draw_data, secondary);
    }

    context.disp.endCommandBuffer(secondary);
    overlay.secondary_generation.at(frame_slot) = FrameSetupFor(data, frame_slot).imgui.ui_generation;
  }

  void DestroyImguiHeaps(vulkan::Context &context, ImGuiOverlayState &overlay)
  {
    overlay.resource_mapped = nullptr;
    overlay.sampler_mapped = nullptr;
    context.gpu_allocator.destroy_buffer(overlay.resource_heap);
    context.gpu_allocator.destroy_buffer(overlay.sampler_heap);
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

auto InitImguiOverlay(vulkan::Context &context, RenderData &data) -> std::expected<void, Error>
{
  if (!LoadImguiVulkanFunctions(context)) {
    return std::unexpected{ MakeError(std::errc::function_not_supported, "failed to load ImGui Vulkan functions") };
  }

  auto overlay = std::make_unique<ImGuiOverlayState>();
  if (context.vkexec_context == nullptr) {
    return std::unexpected{ MakeError(std::errc::function_not_supported, "vkexec context missing for ImGui heaps") };
  }
  overlay->vkexec_context = context.vkexec_context.get();
  overlay->gpu_allocator = &context.gpu_allocator;

  VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
  heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
  VkPhysicalDeviceProperties2 props2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &heap_props,
    .properties = {},
  };
  context.inst_disp.getPhysicalDeviceProperties2(context.device.physical_device, &props2);

  VkDeviceSize const resource_descriptors_size = heap_props.imageDescriptorSize * kImguiImageSlots;
  VkDeviceSize const sampler_descriptors_size = heap_props.samplerDescriptorSize * kImguiSamplerSlots;
  VkDeviceSize const resource_size = AlignBufferSize(
    resource_descriptors_size + heap_props.minResourceHeapReservedRange, heap_props.resourceHeapAlignment);
  VkDeviceSize const sampler_size =
    AlignBufferSize(sampler_descriptors_size + heap_props.minSamplerHeapReservedRange, heap_props.samplerHeapAlignment);

  auto resource_heap = context.gpu_allocator.create_heap_buffer(resource_size);
  if (!resource_heap) {
    return std::unexpected{ MakeError(std::errc::not_enough_memory, "failed to create ImGui resource heap") };
  }
  auto sampler_heap = context.gpu_allocator.create_heap_buffer(sampler_size);
  if (!sampler_heap) {
    context.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ MakeError(std::errc::not_enough_memory, "failed to create ImGui sampler heap") };
  }

  auto resource_mapped = context.gpu_allocator.map_buffer(*resource_heap);
  if (!resource_mapped) {
    context.gpu_allocator.destroy_buffer(*sampler_heap);
    context.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ MakeError(std::errc::io_error, "failed to map ImGui resource heap") };
  }
  auto sampler_mapped = context.gpu_allocator.map_buffer(*sampler_heap);
  if (!sampler_mapped) {
    context.gpu_allocator.destroy_buffer(*sampler_heap);
    context.gpu_allocator.destroy_buffer(*resource_heap);
    return std::unexpected{ MakeError(std::errc::io_error, "failed to map ImGui sampler heap") };
  }

  overlay->resource_heap = std::move(*resource_heap);
  overlay->sampler_heap = std::move(*sampler_heap);
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
  overlay->resource_bind = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = {
      .address = context.gpu_allocator.get_buffer_device_address(overlay->resource_heap),
      .size = overlay->resource_heap_size,
    },
    .reservedRangeOffset = overlay->resource_reserved_offset,
    .reservedRangeSize = overlay->resource_reserved_size,
  };
  overlay->sampler_bind = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = {
      .address = context.gpu_allocator.get_buffer_device_address(overlay->sampler_heap),
      .size = overlay->sampler_heap_size,
    },
    .reservedRangeOffset = overlay->sampler_reserved_offset,
    .reservedRangeSize = overlay->sampler_reserved_size,
  };
  overlay->heap_info = {
    .RegisterImage = RegisterImage,
    .UnRegisterImage = UnregisterImage,
    .RegisterSampler = RegisterSampler,
    .UnRegisterSampler = UnregisterSampler,
    .UserContext = overlay.get(),
  };

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  auto *glfw_window = static_cast<GLFWwindow *>(context.platform->native_window());
  if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
    DestroyImguiHeaps(context, *overlay);
    ImGui::DestroyContext();
    return std::unexpected{ MakeError(std::errc::io_error, "failed to initialize ImGui GLFW backend") };
  }

  FillPipelineRenderingInfo(context, *overlay);

  ImGui_ImplVulkan_InitInfo vulkan_init{};
  vulkan_init.ApiVersion = VK_API_VERSION_1_4;
  vulkan_init.Instance = context.instance;
  vulkan_init.PhysicalDevice = context.device.physical_device;
  vulkan_init.Device = context.device;
  vulkan_init.QueueFamily = context.device.get_queue_index(vkb::QueueType::graphics).value();
  vulkan_init.Queue = data.graphics_queue;
  vulkan_init.MinImageCount = static_cast<uint32_t>(context.swapchain->image_count());
  vulkan_init.ImageCount = static_cast<uint32_t>(context.swapchain->image_count());
  vulkan_init.UseDynamicRendering = true;
  vulkan_init.PipelineInfoMain.PipelineRenderingCreateInfo = overlay->pipeline_rendering;
  vulkan_init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  vulkan_init.CheckVkResultFn = CheckImguiVkResult;
  vulkan_init.DescriptorHeapInfo = &overlay->heap_info;

  if (!ImGui_ImplVulkan_Init(&vulkan_init)) {
    ImGui_ImplGlfw_Shutdown();
    DestroyImguiHeaps(context, *overlay);
    ImGui::DestroyContext();
    return std::unexpected{ MakeError(std::errc::io_error, "failed to initialize ImGui Vulkan backend") };
  }

  overlay->initialized = true;
  data.imgui = std::move(overlay);

  if (!AllocateOverlaySecondaries(data)) {
    ShutdownImguiOverlay(context, data);
    return std::unexpected{ MakeError(std::errc::io_error, "failed to allocate ImGui overlay command buffers") };
  }

  return {};
}

void ShutdownImguiOverlay(vulkan::Context &context, RenderData &data)
{
  if (data.imgui == nullptr) { return; }

  ClearClonedDrawData(*data.imgui);

  if (data.imgui->initialized) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    data.imgui->initialized = false;
  }

  DestroyImguiHeaps(context, *data.imgui);
  data.imgui.reset();
}

void RecreateImguiOverlayPipeline(vulkan::Context const &context, RenderData &data)
{
  if (data.imgui == nullptr || !data.imgui->initialized) { return; }

  FillPipelineRenderingInfo(context, *data.imgui);
  ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(context.swapchain->image_count()));

  ImGui_ImplVulkan_PipelineInfo pipeline_info{};
  pipeline_info.PipelineRenderingCreateInfo = data.imgui->pipeline_rendering;
  pipeline_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_CreateMainPipeline(&pipeline_info);

  // Command pool was recreated with the swapchain; reclaim secondary CBs.
  if (!AllocateOverlaySecondaries(data)) {
    std::println(stderr, "[imgui] failed to reallocate overlay command buffers after swapchain recreate");
  }
  InvalidateOverlaySecondaries(*data.imgui);
  ClearClonedDrawData(*data.imgui);
  data.imgui->last_extent = {};
}

// cppcheck-suppress constParameterReference -- mutates RenderData via data.imgui owned state
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

  DestroyClonedDrawData(overlay.frame_draw_data.at(frame_slot));
  overlay.frame_draw_data.at(frame_slot) = CloneImDrawData(ImGui::GetDrawData());
  // Ensure the matching secondary is re-recorded against this slot's clone.
  overlay.secondary_generation.at(frame_slot) = 0;
}

void RecordImguiOverlay(vulkan::Context &context, RenderData const &data, VkCommandBuffer command_buffer, size_t image_index)
{
  VkImage swapchain_image = context.swapchain->images().at(image_index);

  // Raster blits into TRANSFER_DST; always finish with PRESENT.
  if (data.imgui == nullptr || !data.imgui->initialized) {
    vkexec::image_barrier(command_buffer,
      {
        .image = swapchain_image,
        .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dst_access = 0,
      });
    return;
  }

  auto &overlay = *data.imgui;

  VkExtent2D const extent = context.swapchain->vk_extent();
  if (extent.width != overlay.last_extent.width || extent.height != overlay.last_extent.height) {
    overlay.last_extent = extent;
    InvalidateOverlaySecondaries(overlay);
  }

  size_t const frame_slot = data.current_slot;
  if (overlay.overlay_secondaries.at(frame_slot) == VK_NULL_HANDLE) {
    vkexec::image_barrier(command_buffer,
      {
        .image = swapchain_image,
        .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dst_access = 0,
      });
    return;
  }

  if (overlay.secondary_generation.at(frame_slot) != FrameSetupFor(data, frame_slot).imgui.ui_generation) {
    RecordOverlaySecondary(context, data, frame_slot);
  }

  if (context.vkexec_context != nullptr) {
    auto const resource_bound = vkexec::cmd_bind_resource_heap(*context.vkexec_context,
      command_buffer,
      overlay.resource_bind.heapRange.address,
      overlay.resource_bind.heapRange.size,
      overlay.resource_bind.reservedRangeOffset,
      overlay.resource_bind.reservedRangeSize);
    if (!resource_bound) {
      std::println(stderr, "[imgui] cmd_bind_resource_heap failed: {}", resource_bound.error().message());
    }
    auto const sampler_bound = vkexec::cmd_bind_sampler_heap(*context.vkexec_context,
      command_buffer,
      overlay.sampler_bind.heapRange.address,
      overlay.sampler_bind.heapRange.size,
      overlay.sampler_bind.reservedRangeOffset,
      overlay.sampler_bind.reservedRangeSize);
    if (!sampler_bound) {
      std::println(stderr, "[imgui] cmd_bind_sampler_heap failed: {}", sampler_bound.error().message());
    }
  }

  VkImageView swapchain_view = context.swapchain->image_views().at(image_index);

  vkexec::image_barrier(command_buffer,
    {
      .image = swapchain_image,
      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    });

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

  context.disp.cmdBeginRendering(command_buffer, &rendering_info);
  context.disp.cmdExecuteCommands(command_buffer, 1, &overlay.overlay_secondaries.at(frame_slot));
  context.disp.cmdEndRendering(command_buffer);

  vkexec::image_barrier(command_buffer,
    {
      .image = swapchain_image,
      .old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      .src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dst_access = 0,
    });
}

}// namespace vkgsplat
