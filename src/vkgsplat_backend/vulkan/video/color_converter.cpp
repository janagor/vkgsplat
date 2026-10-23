#include "color_converter.hpp"

#include "shader.hpp"
#include "video_capabilities.hpp"
#include "video_device.hpp"
#include "video_encoder.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/example_config.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan::video {
namespace {

  constexpr std::uint32_t kOneResource = 1U;
  constexpr std::uint32_t kPlaneCount = 2U;
  constexpr std::uint32_t kChromaDivisor = 2U;
  constexpr std::uint32_t kRgbaBytesPerPixel = 4U;
  constexpr std::uint32_t kMacroWidth = 4U;
  constexpr std::uint32_t kMacroHeight = 2U;
  constexpr std::uint32_t kWorkgroupWidth = 8U;
  constexpr std::uint32_t kWorkgroupHeight = 8U;

  [[nodiscard]] auto Failure(char const *operation, VkResult result) -> Error
  {
    return MakeError(result == VK_ERROR_DEVICE_LOST ? std::errc::no_such_device : std::errc::io_error,
      std::string{ operation } + " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
  }

  [[nodiscard]] auto Allocate(Context &context,
    VkMemoryRequirements const &requirements,
    VkMemoryPropertyFlags required,
    VkImage image,
    VkBuffer buffer) -> std::expected<VkDeviceMemory, Error>
  {
    VkPhysicalDeviceMemoryProperties properties{};
    context.inst_disp.getPhysicalDeviceMemoryProperties(context.device.physical_device, &properties);
    for (std::uint32_t index = 0U; index < properties.memoryTypeCount; ++index) {
      if ((requirements.memoryTypeBits & (1U << index)) == 0U
          || (std::span{ properties.memoryTypes }.subspan(index).front().propertyFlags & required) != required) {
        continue;
      }
      VkMemoryDedicatedAllocateInfo dedicated{};
      dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
      dedicated.image = image;
      dedicated.buffer = buffer;
      VkMemoryAllocateInfo allocation{};
      allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocation.pNext = &dedicated;
      allocation.allocationSize = requirements.size;
      allocation.memoryTypeIndex = index;
      VkDeviceMemory memory = VK_NULL_HANDLE;
      auto const result = context.disp.allocateMemory(&allocation, nullptr, &memory);
      if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate video conversion memory", result)); }
      return memory;
    }
    return std::unexpected(MakeError(std::errc::not_enough_memory, "no video conversion memory type"));
  }

  [[nodiscard]] auto CreateBuffer(Context &context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer &buffer,
    VkDeviceMemory &memory) -> std::expected<void, Error>
  {
    VkBufferCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create.size = size;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    auto result = context.disp.createBuffer(&create, nullptr, &buffer);
    if (result != VK_SUCCESS) { return std::unexpected(Failure("create video conversion buffer", result)); }
    VkMemoryRequirements requirements{};
    context.disp.getBufferMemoryRequirements(buffer, &requirements);
    auto allocated = Allocate(context, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_NULL_HANDLE, buffer);
    if (!allocated) { return std::unexpected(allocated.error()); }
    memory = *allocated;
    result = context.disp.bindBufferMemory(buffer, memory, 0U);
    if (result != VK_SUCCESS) { return std::unexpected(Failure("bind video conversion buffer", result)); }
    return {};
  }

  [[nodiscard]] auto LoadShader(Context &context) -> std::expected<VkShaderModule, Error>
  {
    auto const path = std::string{ kShaderDirectory } + "/rgba_to_nv12.comp.spv";
    auto const code = ReadFile(path);
    if (code.empty() || code.size() % sizeof(std::uint32_t) != 0U) {
      return std::unexpected(MakeError(std::errc::invalid_argument, "invalid RGBA-to-NV12 shader"));
    }
    std::vector<std::uint32_t> words(code.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), code.data(), code.size());
    VkShaderModuleCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create.codeSize = code.size();
    create.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    auto const result = context.disp.createShaderModule(&create, nullptr, &module);
    if (result != VK_SUCCESS) { return std::unexpected(Failure("create RGBA-to-NV12 shader", result)); }
    return module;
  }

}// namespace

GpuColorConverter::GpuColorConverter(Context &context, VkExtent2D extent) noexcept
  : context_{ context }, extent_{ extent }
{}

GpuColorConverter::~GpuColorConverter()
{
  if (in_flight_ && converted_done_ != VK_NULL_HANDLE) {
    context_.disp.waitForFences(kOneResource, &converted_done_, VK_TRUE, UINT64_MAX);
  }
  if (converted_done_ != VK_NULL_HANDLE) { context_.disp.destroyFence(converted_done_, nullptr); }
  if (converted_ != VK_NULL_HANDLE) { context_.disp.destroySemaphore(converted_, nullptr); }
  if (command_pool_ != VK_NULL_HANDLE) { context_.disp.destroyCommandPool(command_pool_, nullptr); }
  if (pipeline_ != VK_NULL_HANDLE) { context_.disp.destroyPipeline(pipeline_, nullptr); }
  if (pipeline_layout_ != VK_NULL_HANDLE) { context_.disp.destroyPipelineLayout(pipeline_layout_, nullptr); }
  if (descriptor_pool_ != VK_NULL_HANDLE) { context_.disp.destroyDescriptorPool(descriptor_pool_, nullptr); }
  if (set_layout_ != VK_NULL_HANDLE) { context_.disp.destroyDescriptorSetLayout(set_layout_, nullptr); }
  if (output_view_ != VK_NULL_HANDLE) { context_.disp.destroyImageView(output_view_, nullptr); }
  if (output_ != VK_NULL_HANDLE) { context_.disp.destroyImage(output_, nullptr); }
  if (rgba_buffer_ != VK_NULL_HANDLE) { context_.disp.destroyBuffer(rgba_buffer_, nullptr); }
  if (nv12_buffer_ != VK_NULL_HANDLE) { context_.disp.destroyBuffer(nv12_buffer_, nullptr); }
  if (output_memory_ != VK_NULL_HANDLE) { context_.disp.freeMemory(output_memory_, nullptr); }
  if (rgba_memory_ != VK_NULL_HANDLE) { context_.disp.freeMemory(rgba_memory_, nullptr); }
  if (nv12_memory_ != VK_NULL_HANDLE) { context_.disp.freeMemory(nv12_memory_, nullptr); }
}

auto GpuColorConverter::Create(Context &context, EncodeDevice const &device, VkExtent2D extent)
  -> std::expected<std::unique_ptr<GpuColorConverter>, Error>
{
  if (extent.width == 0U || extent.height == 0U || extent.width % kMacroWidth != 0U
      || extent.height % kMacroHeight != 0U
      || device.selection.source_format.format != VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
      || !device.selection.source_format.transfer_destination) {
    return std::unexpected(MakeError(std::errc::function_not_supported,
      "GPU conversion requires aligned RGBA8 input and NV12 transfer-destination support"));
  }
  auto graphics = context.device.get_queue_and_index(vkb::QueueType::graphics);
  if (!graphics || device.queue == VK_NULL_HANDLE) {
    return std::unexpected(MakeError(std::errc::function_not_supported, "graphics or video queue unavailable"));
  }
  std::uint32_t family_count = 0U;
  context.inst_disp.getPhysicalDeviceQueueFamilyProperties(context.device.physical_device, &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> families_properties(family_count);
  context.inst_disp.getPhysicalDeviceQueueFamilyProperties(
    context.device.physical_device, &family_count, families_properties.data());
  if (graphics->second >= family_count
      || (families_properties.at(graphics->second).queueFlags & VK_QUEUE_COMPUTE_BIT) == 0U) {
    return std::unexpected(
      MakeError(std::errc::function_not_supported, "graphics queue does not support compute color conversion"));
  }
  auto converter = std::unique_ptr<GpuColorConverter>{ new GpuColorConverter{ context, extent } };
  converter->graphics_queue_ = graphics->first;
  auto const y_bytes = static_cast<VkDeviceSize>(extent.width) * extent.height;
  auto const rgba_bytes = y_bytes * kRgbaBytesPerPixel;
  auto const nv12_bytes = y_bytes + (y_bytes / kChromaDivisor);
  if (auto created = CreateBuffer(context,
        rgba_bytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        converter->rgba_buffer_,
        converter->rgba_memory_);
    !created) {
    return std::unexpected(created.error());
  }
  if (auto created = CreateBuffer(context,
        nv12_bytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        converter->nv12_buffer_,
        converter->nv12_memory_);
    !created) {
    return std::unexpected(created.error());
  }

  VkVideoEncodeH264ProfileInfoKHR h264{
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
    .pNext = nullptr,
    .stdProfileIdc = device.selection.profile,
  };
  VkVideoProfileInfoKHR profile{};
  profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
  profile.pNext = &h264;
  profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
  profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
  profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
  profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
  VkVideoProfileListInfoKHR profiles{};
  profiles.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
  profiles.profileCount = kOneResource;
  profiles.pProfiles = &profile;
  std::array const families{ graphics->second, device.queue_family_index };
  auto const sharing = families.front() == families.back() ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  auto const &format = device.selection.source_format;
  VkImageCreateInfo const image{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = &profiles,
    .flags = 0,
    .imageType = format.image_type,
    .format = format.format,
    .extent = { .width = extent.width, .height = extent.height, .depth = kOneResource },
    .mipLevels = kOneResource,
    .arrayLayers = kOneResource,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = format.tiling,
    .usage = VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    .sharingMode = sharing,
    .queueFamilyIndexCount = sharing == VK_SHARING_MODE_CONCURRENT ? kPlaneCount : 0U,
    .pQueueFamilyIndices = sharing == VK_SHARING_MODE_CONCURRENT ? families.data() : nullptr,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  auto result = context.disp.createImage(&image, nullptr, &converter->output_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create converted NV12 image", result)); }
  VkMemoryRequirements requirements{};
  context.disp.getImageMemoryRequirements(converter->output_, &requirements);
  auto memory =
    Allocate(context, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, converter->output_, VK_NULL_HANDLE);
  if (!memory) { return std::unexpected(memory.error()); }
  converter->output_memory_ = *memory;
  result = context.disp.bindImageMemory(converter->output_, converter->output_memory_, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("bind converted NV12 image", result)); }
  VkImageViewCreateInfo view{};
  view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view.image = converter->output_;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = format.format;
  view.components = format.components;
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view.subresourceRange.levelCount = kOneResource;
  view.subresourceRange.layerCount = kOneResource;
  result = context.disp.createImageView(&view, nullptr, &converter->output_view_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create converted NV12 image view", result)); }

  std::array<VkDescriptorSetLayoutBinding, kPlaneCount> bindings{};
  std::uint32_t binding_index = 0U;
  std::ranges::for_each(bindings, [&binding_index](VkDescriptorSetLayoutBinding &binding) -> void {
    binding.binding = binding_index++;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = kOneResource;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  });
  VkDescriptorSetLayoutCreateInfo set_layout{};
  set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  set_layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
  set_layout.pBindings = bindings.data();
  result = context.disp.createDescriptorSetLayout(&set_layout, nullptr, &converter->set_layout_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create conversion descriptor layout", result)); }
  VkDescriptorPoolSize const pool_size{
    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = kPlaneCount,
  };
  VkDescriptorPoolCreateInfo pool{};
  pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool.maxSets = kOneResource;
  pool.poolSizeCount = kOneResource;
  pool.pPoolSizes = &pool_size;
  result = context.disp.createDescriptorPool(&pool, nullptr, &converter->descriptor_pool_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create conversion descriptor pool", result)); }
  VkDescriptorSetAllocateInfo set_allocate{};
  set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_allocate.descriptorPool = converter->descriptor_pool_;
  set_allocate.descriptorSetCount = kOneResource;
  set_allocate.pSetLayouts = &converter->set_layout_;
  result = context.disp.allocateDescriptorSets(&set_allocate, &converter->descriptor_set_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate conversion descriptor set", result)); }
  std::array<VkDescriptorBufferInfo, kPlaneCount> const buffer_infos{
    VkDescriptorBufferInfo{ .buffer = converter->rgba_buffer_, .offset = 0U, .range = rgba_bytes },
    VkDescriptorBufferInfo{ .buffer = converter->nv12_buffer_, .offset = 0U, .range = nv12_bytes },
  };
  std::array<VkWriteDescriptorSet, kPlaneCount> writes{};
  std::uint32_t write_index = 0U;
  std::ranges::for_each(writes, [&write_index, &converter, &buffer_infos](VkWriteDescriptorSet &write) -> void {
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = converter->descriptor_set_;
    write.dstBinding = write_index;
    write.descriptorCount = kOneResource;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer_infos.at(write_index);
    ++write_index;
  });
  context.disp.updateDescriptorSets(static_cast<std::uint32_t>(writes.size()), writes.data(), 0U, nullptr);
  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push.size = sizeof(VkExtent2D);
  VkPipelineLayoutCreateInfo pipeline_layout{};
  pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout.setLayoutCount = kOneResource;
  pipeline_layout.pSetLayouts = &converter->set_layout_;
  pipeline_layout.pushConstantRangeCount = kOneResource;
  pipeline_layout.pPushConstantRanges = &push;
  result = context.disp.createPipelineLayout(&pipeline_layout, nullptr, &converter->pipeline_layout_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create conversion pipeline layout", result)); }
  auto module = LoadShader(context);
  if (!module) { return std::unexpected(module.error()); }
  VkComputePipelineCreateInfo const compute{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .stage =
      VkPipelineShaderStageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = *module,
        .pName = "main",
        .pSpecializationInfo = nullptr,
      },
    .layout = converter->pipeline_layout_,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = 0,
  };
  result = context.disp.createComputePipelines(VK_NULL_HANDLE, kOneResource, &compute, nullptr, &converter->pipeline_);
  context.disp.destroyShaderModule(*module, nullptr);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create RGBA-to-NV12 pipeline", result)); }
  VkCommandPoolCreateInfo command_pool{};
  command_pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  command_pool.queueFamilyIndex = graphics->second;
  result = context.disp.createCommandPool(&command_pool, nullptr, &converter->command_pool_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create conversion command pool", result)); }
  VkCommandBufferAllocateInfo command_allocate{};
  command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_allocate.commandPool = converter->command_pool_;
  command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_allocate.commandBufferCount = kOneResource;
  result = context.disp.allocateCommandBuffers(&command_allocate, &converter->command_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate conversion command buffer", result)); }
  VkSemaphoreCreateInfo semaphore{};
  semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  result = context.disp.createSemaphore(&semaphore, nullptr, &converter->converted_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create conversion semaphore", result)); }
  VkFenceCreateInfo fence{};
  fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  result = context.disp.createFence(&fence, nullptr, &converter->converted_done_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create conversion fence", result)); }
  return converter;
}

auto GpuColorConverter::WaitForSourceRelease() -> std::expected<void, Error>
{
  if (!in_flight_) { return {}; }
  auto const result = context_.disp.waitForFences(kOneResource, &converted_done_, VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) {
    // A lease may release its RGBA source only after conversion has stopped
    // reading it. A queue-wide wait is reserved for this exceptional path.
    auto const queue_lock = context_.vkexec_context->lock_queue(graphics_queue_);
    auto const idle = context_.disp.queueWaitIdle(graphics_queue_);
    if (idle != VK_SUCCESS) { return std::unexpected(Failure("retire failed GPU color conversion", idle)); }
  }
  return {};
}

auto GpuColorConverter::Complete() -> std::expected<void, Error>
{
  if (auto waited = WaitForSourceRelease(); !waited) { return waited; }
  in_flight_ = false;
  return {};
}

auto GpuColorConverter::Convert(VkImage source, VkSemaphore ready_timeline, std::uint64_t ready_value)
  -> std::expected<PreparedVideoFrame, Error>
{
  if (failed_) {
    return std::unexpected(
      MakeError(std::errc::state_not_recoverable, "GPU color converter failed; create a new recording session"));
  }
  if (in_flight_ || source == VK_NULL_HANDLE || ready_timeline == VK_NULL_HANDLE) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid or busy GPU color conversion"));
  }
  auto result = context_.disp.resetCommandBuffer(command_, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("reset conversion command", result)); }
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  result = context_.disp.beginCommandBuffer(command_, &begin);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("begin conversion command", result)); }
  auto const y_bytes = static_cast<VkDeviceSize>(extent_.width) * extent_.height;
  VkBufferImageCopy rgba_copy{};
  rgba_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  rgba_copy.imageSubresource.layerCount = kOneResource;
  rgba_copy.imageExtent = { .width = extent_.width, .height = extent_.height, .depth = kOneResource };
  context_.disp.cmdCopyImageToBuffer(
    command_, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rgba_buffer_, kOneResource, &rgba_copy);
  VkBufferMemoryBarrier2 rgba_barrier{};
  rgba_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
  rgba_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  rgba_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  rgba_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  rgba_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
  rgba_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  rgba_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  rgba_barrier.buffer = rgba_buffer_;
  rgba_barrier.size = y_bytes * kRgbaBytesPerPixel;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.bufferMemoryBarrierCount = kOneResource;
  dependency.pBufferMemoryBarriers = &rgba_barrier;
  context_.disp.cmdPipelineBarrier2(command_, &dependency);
  context_.disp.cmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
  context_.disp.cmdBindDescriptorSets(
    command_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0U, kOneResource, &descriptor_set_, 0U, nullptr);
  context_.disp.cmdPushConstants(
    command_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0U, sizeof(extent_), &extent_);
  auto const groups_x = ((extent_.width / kMacroWidth) + kWorkgroupWidth - 1U) / kWorkgroupWidth;
  auto const groups_y = ((extent_.height / kMacroHeight) + kWorkgroupHeight - 1U) / kWorkgroupHeight;
  context_.disp.cmdDispatch(command_, groups_x, groups_y, kOneResource);
  VkBufferMemoryBarrier2 nv12_barrier{};
  nv12_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
  nv12_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  nv12_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
  nv12_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  nv12_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
  nv12_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  nv12_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  nv12_barrier.buffer = nv12_buffer_;
  nv12_barrier.size = y_bytes + (y_bytes / kChromaDivisor);
  VkImageMemoryBarrier2 image_barrier{};
  image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
  image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_barrier.image = output_;
  image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_barrier.subresourceRange.levelCount = kOneResource;
  image_barrier.subresourceRange.layerCount = kOneResource;
  dependency.bufferMemoryBarrierCount = kOneResource;
  dependency.pBufferMemoryBarriers = &nv12_barrier;
  dependency.imageMemoryBarrierCount = kOneResource;
  dependency.pImageMemoryBarriers = &image_barrier;
  context_.disp.cmdPipelineBarrier2(command_, &dependency);
  std::array<VkBufferImageCopy, kPlaneCount> copies{};
  auto &luma_copy = copies.at(0U);
  luma_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
  luma_copy.imageSubresource.layerCount = kOneResource;
  luma_copy.imageExtent = { .width = extent_.width, .height = extent_.height, .depth = kOneResource };
  auto &chroma_copy = copies.at(1U);
  chroma_copy.bufferOffset = y_bytes;
  chroma_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
  chroma_copy.imageSubresource.layerCount = kOneResource;
  chroma_copy.imageExtent = {
    .width = extent_.width / kChromaDivisor, .height = extent_.height / kChromaDivisor, .depth = kOneResource
  };
  context_.disp.cmdCopyBufferToImage(command_,
    nv12_buffer_,
    output_,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    static_cast<std::uint32_t>(copies.size()),
    copies.data());
  image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  image_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
  image_barrier.dstAccessMask = 0U;
  image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  image_barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
  dependency.bufferMemoryBarrierCount = 0U;
  dependency.pBufferMemoryBarriers = nullptr;
  context_.disp.cmdPipelineBarrier2(command_, &dependency);
  result = context_.disp.endCommandBuffer(command_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("end conversion command", result)); }
  VkSemaphoreSubmitInfo wait{};
  wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  wait.semaphore = ready_timeline;
  wait.value = ready_value;
  wait.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  VkSemaphoreSubmitInfo signal{};
  signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signal.semaphore = converted_;
  signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkCommandBufferSubmitInfo command{};
  command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  command.commandBuffer = command_;
  VkSubmitInfo2 submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submit.waitSemaphoreInfoCount = kOneResource;
  submit.pWaitSemaphoreInfos = &wait;
  submit.commandBufferInfoCount = kOneResource;
  submit.pCommandBufferInfos = &command;
  submit.signalSemaphoreInfoCount = kOneResource;
  submit.pSignalSemaphoreInfos = &signal;
  result = context_.disp.resetFences(kOneResource, &converted_done_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("reset conversion fence", result)); }
  if (fail_next_submission_for_test_) {
    fail_next_submission_for_test_ = false;
    failed_ = true;
    return std::unexpected(MakeError(std::errc::io_error, "injected conversion submission failure"));
  }
  auto const queue_lock = context_.vkexec_context->lock_queue(graphics_queue_);
  result = context_.disp.queueSubmit2(graphics_queue_, kOneResource, &submit, converted_done_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("submit GPU color conversion", result)); }
  in_flight_ = true;
  return PreparedVideoFrame{
    .image_view = output_view_,
    .extent = extent_,
    .format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
    .layout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
    .ready_binary_semaphore = converted_,
  };
}

ConversionLease::ConversionLease(std::shared_ptr<GpuColorConverterPool> pool, std::size_t slot) noexcept
  : pool_{ std::move(pool) }, slot_{ slot }
{}

ConversionLease::~ConversionLease() noexcept { pool_->Release(slot_, encode_submitted_); }

GpuColorConverterPool::GpuColorConverterPool(Context &context, EncodeDevice device, VkExtent2D extent)
  : context_{ context }, device_{ std::move(device) }, extent_{ extent }
{}

auto GpuColorConverterPool::Create(Context &context,
  EncodeDevice const &device,
  VkExtent2D extent,
  std::size_t slot_count) -> std::expected<std::shared_ptr<GpuColorConverterPool>, Error>
{
  if (slot_count == 0U || slot_count > VideoEncoder::kMaximumInFlightFrames) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid conversion slot count"));
  }
  auto pool = std::shared_ptr<GpuColorConverterPool>{ new GpuColorConverterPool{ context, device, extent } };
  pool->source_owners_.resize(slot_count);
  pool->converters_.reserve(slot_count);
  pool->states_.resize(slot_count, SlotState::kFree);
  pool->encode_submitted_.resize(slot_count, false);
  for (std::size_t slot = 0U; slot < slot_count; ++slot) {
    auto converter = GpuColorConverter::Create(context, device, extent);
    if (!converter) { return std::unexpected(converter.error()); }
    pool->converters_.push_back(std::move(*converter));
  }
  return pool;
}

auto GpuColorConverterPool::Convert(VkImage source,
  VkSemaphore ready_timeline,
  std::uint64_t ready_value,
  std::shared_ptr<void> source_owner) -> std::expected<ConvertedFrame, Error>
{
  if (!source_owner) {
    return std::unexpected(
      MakeError(std::errc::invalid_argument, "asynchronous conversion requires an owned RGBA source"));
  }
  std::unique_lock lock{ mutex_ };
  if (failure_) { return std::unexpected(*failure_); }
  for (std::size_t slot = 0U; slot < converters_.size(); ++slot) {
    if (states_.at(slot) != SlotState::kFree) { continue; }
    auto lease = std::shared_ptr<ConversionLease>{ new ConversionLease{ shared_from_this(), slot } };
    states_.at(slot) = SlotState::kLeased;
    source_owners_.at(slot) = std::move(source_owner);
    lock.unlock();
    auto prepared = converters_.at(slot)->Convert(source, ready_timeline, ready_value);
    if (!prepared) {
      std::scoped_lock const failure_lock{ mutex_ };
      failure_ = prepared.error();
      return std::unexpected(prepared.error());
    }
    return ConvertedFrame{ .prepared = *prepared, .lease = std::move(lease) };
  }
  return std::unexpected(MakeError(std::errc::resource_unavailable_try_again, "all GPU conversion slots are busy"));
}

auto GpuColorConverterPool::Release(std::size_t slot, bool encode_submitted) noexcept -> void
{
  std::scoped_lock const lock{ mutex_ };
  encode_submitted_.at(slot) = encode_submitted;
  states_.at(slot) = SlotState::kRetiring;
}

auto GpuColorConverterPool::RetireReleased() -> std::expected<void, Error>
{
  {
    std::scoped_lock const lock{ mutex_ };
    if (failure_ && failure_->code() == std::errc::no_such_device) { return std::unexpected(*failure_); }
  }
  for (std::size_t slot = 0U; slot < converters_.size(); ++slot) {
    bool encode_submitted = false;
    {
      std::scoped_lock const lock{ mutex_ };
      if (states_.at(slot) != SlotState::kRetiring && states_.at(slot) != SlotState::kFailed) { continue; }
      states_.at(slot) = SlotState::kRetiringGpu;
      encode_submitted = encode_submitted_.at(slot);
    }
    std::expected<void, Error> completed;
    if (converters_.at(slot) != nullptr) { completed = converters_.at(slot)->Complete(); }
    if (!completed) {
      std::scoped_lock const lock{ mutex_ };
      states_.at(slot) = SlotState::kFailed;
      failure_ = completed.error();
      return std::unexpected(completed.error());
    }
    source_owners_.at(slot).reset();
    if (!encode_submitted) {
      // Destroying the completed converter discards its unconsumed binary
      // semaphore. A fresh converter has a fresh semaphore and command state.
      converters_.at(slot).reset();
      auto replacement = GpuColorConverter::Create(context_, device_, extent_);
      if (!replacement) {
        std::scoped_lock const lock{ mutex_ };
        states_.at(slot) = SlotState::kFailed;
        failure_ = replacement.error();
        return std::unexpected(replacement.error());
      }
      converters_.at(slot) = std::move(*replacement);
    }
    {
      std::scoped_lock const lock{ mutex_ };
      states_.at(slot) = SlotState::kFree;
      encode_submitted_.at(slot) = false;
    }
  }
  {
    std::scoped_lock const lock{ mutex_ };
    auto const still_retiring = std::ranges::any_of(states_, [](SlotState state) -> bool {
      return state == SlotState::kRetiring || state == SlotState::kRetiringGpu || state == SlotState::kFailed;
    });
    if (!still_retiring) { failure_.reset(); }
  }
  return {};
}

auto GpuColorConverterPool::PollError() const -> std::optional<Error>
{
  std::scoped_lock const lock{ mutex_ };
  return failure_;
}

}// namespace vkgsplat::vulkan::video
