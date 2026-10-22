// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include "video_encode_gpu_source.hpp"
#include "../src/vkgsplat_backend/vulkan/video/video_device.hpp"
#include "../src/vkgsplat_backend/vulkan/video/video_encoder.hpp"
#include "../src/vkgsplat_backend/vulkan/vulkan_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <system_error>

#include <VkBootstrap.h>
#include <vulkan/vulkan_core.h>
#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::test {
namespace {

  constexpr std::uint32_t kOneResource = 1U;
  constexpr std::uint32_t kChromaDivisor = 2U;
  constexpr std::uint8_t kNeutralChroma = 128U;
  constexpr std::size_t kLumaPlane = 0U;
  constexpr std::size_t kChromaPlane = 1U;
  constexpr std::size_t kPlaneCount = 2U;

  [[nodiscard]] auto Failure(char const *operation, VkResult result) -> Error
  {
    return MakeError(std::errc::io_error,
      std::string{ operation } + " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
  }

  [[nodiscard]] auto AllocateMemory(vulkan::Context &context, VkMemoryRequirements const &requirements,
    VkMemoryPropertyFlags required, VkImage image, VkBuffer buffer) -> std::expected<VkDeviceMemory, Error>
  {
    VkPhysicalDeviceMemoryProperties properties{};
    context.inst_disp.getPhysicalDeviceMemoryProperties(context.device.physical_device, &properties);
    for (std::uint32_t index = 0U; index < properties.memoryTypeCount; ++index) {
      if ((requirements.memoryTypeBits & (1U << index)) == 0U
          || (std::span{ properties.memoryTypes }.subspan(index).front().propertyFlags & required) != required) { continue; }
      VkMemoryDedicatedAllocateInfo const dedicated{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = nullptr,
        .image = image,
        .buffer = buffer,
      };
      VkMemoryAllocateInfo const allocation{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicated,
        .allocationSize = requirements.size,
        .memoryTypeIndex = index,
      };
      VkDeviceMemory memory = VK_NULL_HANDLE;
      auto const result = context.disp.allocateMemory(&allocation, nullptr, &memory);
      if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate GPU test memory", result)); }
      return memory;
    }
    return std::unexpected(MakeError(std::errc::not_enough_memory, "no compatible GPU test memory type"));
  }

}// namespace

GpuVideoSource::GpuVideoSource(vulkan::Context &context, VkExtent2D extent) noexcept
  : context_{ context }, extent_{ extent }
{}

GpuVideoSource::~GpuVideoSource()
{
  if (in_flight_ && graphics_queue_ != VK_NULL_HANDLE) { context_.disp.queueWaitIdle(graphics_queue_); }
  if (ready_ != VK_NULL_HANDLE) { context_.disp.destroySemaphore(ready_, nullptr); }
  if (pool_ != VK_NULL_HANDLE) { context_.disp.destroyCommandPool(pool_, nullptr); }
  if (upload_ != VK_NULL_HANDLE) { context_.disp.destroyBuffer(upload_, nullptr); }
  if (view_ != VK_NULL_HANDLE) { context_.disp.destroyImageView(view_, nullptr); }
  if (image_ != VK_NULL_HANDLE) { context_.disp.destroyImage(image_, nullptr); }
  if (upload_memory_ != VK_NULL_HANDLE) { context_.disp.freeMemory(upload_memory_, nullptr); }
  if (image_memory_ != VK_NULL_HANDLE) { context_.disp.freeMemory(image_memory_, nullptr); }
}

auto GpuVideoSource::Create(vulkan::Context &context, vulkan::video::EncodeDevice const &device,
  VkExtent2D extent) -> std::expected<std::unique_ptr<GpuVideoSource>, Error>
{
  if (device.selection.source_format.format != VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
      || !device.selection.source_format.transfer_destination) {
    return std::unexpected(MakeError(std::errc::function_not_supported,
      "external GPU test source requires a two-plane NV12 transfer destination"));
  }
  auto graphics = context.device.get_queue_and_index(vkb::QueueType::graphics);
  if (!graphics || device.queue == VK_NULL_HANDLE) {
    return std::unexpected(MakeError(std::errc::function_not_supported, "GPU test requires graphics and video queues"));
  }
  auto source = std::unique_ptr<GpuVideoSource>{ new GpuVideoSource{ context, extent } };
  source->graphics_queue_ = graphics->first;
  VkVideoEncodeH264ProfileInfoKHR const h264{
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
    .pNext = nullptr,
    .stdProfileIdc = device.selection.profile,
  };
  VkVideoProfileInfoKHR const profile{
    .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
    .pNext = &h264,
    .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
    .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
    .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
  };
  VkVideoProfileListInfoKHR const profiles{
    .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
    .pNext = nullptr,
    .profileCount = kOneResource,
    .pProfiles = &profile,
  };
  std::array const families{ graphics->second, device.queue_family_index };
  auto const sharing = families.front() == families.back() ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  auto const &format = device.selection.source_format;
  VkImageCreateInfo const image{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = &profiles,
    .flags = 0U,
    .imageType = format.image_type,
    .format = format.format,
    .extent = { .width = extent.width, .height = extent.height, .depth = kOneResource },
    .mipLevels = kOneResource,
    .arrayLayers = kOneResource,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = format.tiling,
    .usage = VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    .sharingMode = sharing,
    .queueFamilyIndexCount = sharing == VK_SHARING_MODE_CONCURRENT ? static_cast<std::uint32_t>(families.size()) : 0U,
    .pQueueFamilyIndices = sharing == VK_SHARING_MODE_CONCURRENT ? families.data() : nullptr,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  auto result = context.disp.createImage(&image, nullptr, &source->image_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create external NV12 image", result)); }
  VkMemoryRequirements requirements{};
  context.disp.getImageMemoryRequirements(source->image_, &requirements);
  auto memory = AllocateMemory(context, requirements, 0U, source->image_, VK_NULL_HANDLE);
  if (!memory) { return std::unexpected(memory.error()); }
  source->image_memory_ = *memory;
  result = context.disp.bindImageMemory(source->image_, source->image_memory_, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("bind external NV12 image", result)); }
  VkImageViewCreateInfo const view{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0U,
    .image = source->image_,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = format.format,
    .components = format.components,
    .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0U,
      .levelCount = kOneResource, .baseArrayLayer = 0U, .layerCount = kOneResource },
  };
  result = context.disp.createImageView(&view, nullptr, &source->view_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create external NV12 image view", result)); }
  auto const y_size = static_cast<VkDeviceSize>(extent.width) * extent.height;
  auto const upload_size = y_size + (y_size / kChromaDivisor);
  VkBufferCreateInfo const buffer{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0U,
    .size = upload_size,
    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  result = context.disp.createBuffer(&buffer, nullptr, &source->upload_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create external NV12 upload", result)); }
  context.disp.getBufferMemoryRequirements(source->upload_, &requirements);
  memory = AllocateMemory(context, requirements,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    VK_NULL_HANDLE, source->upload_);
  if (!memory) { return std::unexpected(memory.error()); }
  source->upload_memory_ = *memory;
  result = context.disp.bindBufferMemory(source->upload_, source->upload_memory_, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("bind external NV12 upload", result)); }
  VkCommandPoolCreateInfo const pool{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = graphics->second,
  };
  result = context.disp.createCommandPool(&pool, nullptr, &source->pool_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create external NV12 command pool", result)); }
  VkCommandBufferAllocateInfo const allocate{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext = nullptr,
    .commandPool = source->pool_,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = kOneResource,
  };
  result = context.disp.allocateCommandBuffers(&allocate, &source->command_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate external NV12 command", result)); }
  VkSemaphoreCreateInfo const semaphore{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
  result = context.disp.createSemaphore(&semaphore, nullptr, &source->ready_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create external NV12 ready semaphore", result)); }
  return source;
}

auto GpuVideoSource::Prepare(std::uint8_t luma) -> std::expected<vulkan::video::PreparedVideoFrame, Error>
{
  if (in_flight_) {
    return std::unexpected(MakeError(std::errc::operation_in_progress, "previous GPU test source is still in use"));
  }
  auto const y_size = static_cast<VkDeviceSize>(extent_.width) * extent_.height;
  auto const upload_size = y_size + (y_size / kChromaDivisor);
  void *mapped = nullptr;
  auto result = context_.disp.mapMemory(upload_memory_, 0U, upload_size, 0U, &mapped);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("map external NV12 upload", result)); }
  std::memset(mapped, luma, static_cast<std::size_t>(y_size));
  auto const upload_bytes = std::span{ static_cast<std::byte *>(mapped), static_cast<std::size_t>(upload_size) };
  std::memset(upload_bytes.subspan(static_cast<std::size_t>(y_size)).data(), kNeutralChroma,
    static_cast<std::size_t>(upload_size - y_size));
  context_.disp.unmapMemory(upload_memory_);
  result = context_.disp.resetCommandBuffer(command_, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("reset external NV12 command", result)); }
  VkCommandBufferBeginInfo const begin{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = nullptr,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  result = context_.disp.beginCommandBuffer(command_, &begin);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("begin external NV12 command", result)); }
  VkImageMemoryBarrier2 barrier{
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
    .srcAccessMask = 0U,
    .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
    .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = image_,
    .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0U,
      .levelCount = kOneResource, .baseArrayLayer = 0U, .layerCount = kOneResource },
  };
  VkDependencyInfo const dependency{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .pNext = nullptr,
    .imageMemoryBarrierCount = kOneResource,
    .pImageMemoryBarriers = &barrier,
  };
  context_.disp.cmdPipelineBarrier2(command_, &dependency);
  std::array<VkBufferImageCopy, kPlaneCount> regions{};
  auto &luma_region = regions.at(kLumaPlane);
  luma_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
  luma_region.imageSubresource.layerCount = kOneResource;
  luma_region.imageExtent = { .width = extent_.width, .height = extent_.height, .depth = kOneResource };
  auto &chroma_region = regions.at(kChromaPlane);
  chroma_region.bufferOffset = y_size;
  chroma_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
  chroma_region.imageSubresource.layerCount = kOneResource;
  chroma_region.imageExtent = { .width = extent_.width / kChromaDivisor,
    .height = extent_.height / kChromaDivisor, .depth = kOneResource };
  context_.disp.cmdCopyBufferToImage(command_, upload_, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    static_cast<std::uint32_t>(regions.size()), regions.data());
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
  barrier.dstAccessMask = 0U;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
  context_.disp.cmdPipelineBarrier2(command_, &dependency);
  result = context_.disp.endCommandBuffer(command_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("end external NV12 command", result)); }
  VkCommandBufferSubmitInfo const command_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    .pNext = nullptr,
    .commandBuffer = command_,
    .deviceMask = 0U,
  };
  VkSemaphoreSubmitInfo const signal{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .pNext = nullptr,
    .semaphore = ready_,
    .value = 0U,
    .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    .deviceIndex = 0U,
  };
  VkSubmitInfo2 const submit{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .pNext = nullptr,
    .waitSemaphoreInfoCount = 0U,
    .pWaitSemaphoreInfos = nullptr,
    .commandBufferInfoCount = kOneResource,
    .pCommandBufferInfos = &command_info,
    .signalSemaphoreInfoCount = kOneResource,
    .pSignalSemaphoreInfos = &signal,
  };
  result = context_.disp.queueSubmit2(graphics_queue_, kOneResource, &submit, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("submit external NV12 upload", result)); }
  in_flight_ = true;
  return vulkan::video::PreparedVideoFrame{
    .image_view = view_,
    .extent = extent_,
    .format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
    .layout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
    .ready_binary_semaphore = ready_,
  };
}

}// namespace vkgsplat::test
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
