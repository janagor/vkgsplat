#include "video_encoder.hpp"

#include "video_capabilities.hpp"
#include "video_device.hpp"
#include "video_session.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat_video/encoded_packet.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <VkBootstrap.h>
#include <vk_video/vulkan_video_codec_h264std.h>
#include <vk_video/vulkan_video_codec_h264std_encode.h>
#include <vulkan/vulkan_core.h>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat::vulkan::video {
namespace {

constexpr std::int32_t kPreferredQp = 26;
constexpr VkDeviceSize kBitstreamBytesPerPixel = 2;
constexpr VkDeviceSize kChromaSubsamplingDivisor = 2;
constexpr std::uint32_t kOneResource = 1;
constexpr size_t kLumaPlaneIndex = 0;
constexpr size_t kChromaPlaneIndex = 1;
constexpr std::array kAnnexBStartCode{
  std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 },
};

[[nodiscard]] auto Failure(char const *operation, VkResult result) -> Error
{
  return vkgsplat::MakeError(result == VK_ERROR_DEVICE_LOST ? std::errc::no_such_device : std::errc::io_error,
    std::string{ operation } + " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
}

}// namespace

struct EncoderResources
{
  Context *context;
  VkQueue graphics_queue = VK_NULL_HANDLE;
  VkQueue video_queue = VK_NULL_HANDLE;
  VkDeviceSize upload_size = 0;
  VkDeviceSize bitstream_size = 0;
  bool in_flight = false;
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory image_memory = VK_NULL_HANDLE;
  VkBuffer upload = VK_NULL_HANDLE;
  VkDeviceMemory upload_memory = VK_NULL_HANDLE;
  VkBuffer bitstream = VK_NULL_HANDLE;
  VkDeviceMemory bitstream_memory = VK_NULL_HANDLE;
  VkBuffer readback = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory = VK_NULL_HANDLE;
  VkCommandPool upload_pool = VK_NULL_HANDLE;
  VkCommandPool video_pool = VK_NULL_HANDLE;
  VkCommandBuffer upload_cmd = VK_NULL_HANDLE;
  VkCommandBuffer video_cmd = VK_NULL_HANDLE;
  VkCommandBuffer readback_cmd = VK_NULL_HANDLE;
  VkSemaphore upload_done = VK_NULL_HANDLE;
  VkSemaphore video_done = VK_NULL_HANDLE;
  VkFence readback_done = VK_NULL_HANDLE;
  VkQueryPool feedback = VK_NULL_HANDLE;

  explicit EncoderResources(Context &value) : context{ &value } {}

  EncoderResources(EncoderResources const &) = delete;
  auto operator=(EncoderResources const &) -> EncoderResources & = delete;
  EncoderResources(EncoderResources &&) = delete;
  auto operator=(EncoderResources &&) -> EncoderResources & = delete;

  ~EncoderResources()
  {
    // A failed partial submission has no completion fence; drain only the queues
    // used by this encoder before releasing its resources.
    if (in_flight) {
      if (graphics_queue != VK_NULL_HANDLE) {
        auto const queue_lock = context->vkexec_context->lock_queue(graphics_queue);
        context->disp.queueWaitIdle(graphics_queue);
      }
      if (video_queue != VK_NULL_HANDLE && video_queue != graphics_queue) {
        auto const queue_lock = context->vkexec_context->lock_queue(video_queue);
        context->disp.queueWaitIdle(video_queue);
      }
    }
    if (feedback != VK_NULL_HANDLE) { context->disp.destroyQueryPool(feedback, nullptr); }
    if (readback_done != VK_NULL_HANDLE) { context->disp.destroyFence(readback_done, nullptr); }
    if (video_done != VK_NULL_HANDLE) { context->disp.destroySemaphore(video_done, nullptr); }
    if (upload_done != VK_NULL_HANDLE) { context->disp.destroySemaphore(upload_done, nullptr); }
    if (video_pool != VK_NULL_HANDLE) { context->disp.destroyCommandPool(video_pool, nullptr); }
    if (upload_pool != VK_NULL_HANDLE) { context->disp.destroyCommandPool(upload_pool, nullptr); }
    if (readback != VK_NULL_HANDLE) { context->disp.destroyBuffer(readback, nullptr); }
    if (bitstream != VK_NULL_HANDLE) { context->disp.destroyBuffer(bitstream, nullptr); }
    if (upload != VK_NULL_HANDLE) { context->disp.destroyBuffer(upload, nullptr); }
    if (view != VK_NULL_HANDLE) { context->disp.destroyImageView(view, nullptr); }
    if (image != VK_NULL_HANDLE) { context->disp.destroyImage(image, nullptr); }
    if (readback_memory != VK_NULL_HANDLE) { context->disp.freeMemory(readback_memory, nullptr); }
    if (bitstream_memory != VK_NULL_HANDLE) { context->disp.freeMemory(bitstream_memory, nullptr); }
    if (upload_memory != VK_NULL_HANDLE) { context->disp.freeMemory(upload_memory, nullptr); }
    if (image_memory != VK_NULL_HANDLE) { context->disp.freeMemory(image_memory, nullptr); }
  }
};

namespace {

[[nodiscard]] auto FindMemoryType(Context &context, std::uint32_t mask,
  VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) -> std::expected<std::uint32_t, Error>
{
  VkPhysicalDeviceMemoryProperties properties{};
  context.inst_disp.getPhysicalDeviceMemoryProperties(context.device.physical_device, &properties);
  auto const find = [&](VkMemoryPropertyFlags flags) -> std::uint32_t {
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
      if ((mask & (1U << index)) != 0
          && (properties.memoryTypes[index].propertyFlags & flags) == flags) { return index; }
    }
    return properties.memoryTypeCount;
  };
  auto type = find(required | preferred);
  if (type == properties.memoryTypeCount) { type = find(required); }
  if (type == properties.memoryTypeCount) {
    return std::unexpected(vkgsplat::MakeError(std::errc::not_enough_memory, "no compatible video resource memory type"));
  }
  return type;
}

[[nodiscard]] auto AllocateImageMemory(Context &context, EncoderResources &resources) -> std::expected<void, Error>
{
  VkMemoryRequirements requirements{};
  context.disp.getImageMemoryRequirements(resources.image, &requirements);
  auto type = FindMemoryType(context, requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!type) { return std::unexpected(type.error()); }
  VkMemoryDedicatedAllocateInfo dedicated{};
  dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicated.image = resources.image;
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.pNext = &dedicated;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = *type;
  auto result = context.disp.allocateMemory(&allocation, nullptr, &resources.image_memory);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate NV12 image", result)); }
  result = context.disp.bindImageMemory(resources.image, resources.image_memory, 0);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("bind NV12 image", result)); }
  return {};
}

[[nodiscard]] auto CreateBuffer(Context &context, VkDeviceSize size, VkBufferUsageFlags usage,
  VkVideoProfileListInfoKHR const *profiles, VkSharingMode sharing, std::span<std::uint32_t const> families,
  VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
  VkBuffer &buffer, VkDeviceMemory &memory) -> std::expected<void, Error>
{
  VkBufferCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  create.pNext = profiles;
  create.size = size;
  create.usage = usage;
  create.sharingMode = sharing;
  create.queueFamilyIndexCount = sharing == VK_SHARING_MODE_CONCURRENT
                                   ? static_cast<std::uint32_t>(families.size()) : 0;
  create.pQueueFamilyIndices = sharing == VK_SHARING_MODE_CONCURRENT ? families.data() : nullptr;
  auto result = context.disp.createBuffer(&create, nullptr, &buffer);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create video buffer", result)); }
  VkMemoryRequirements requirements{};
  context.disp.getBufferMemoryRequirements(buffer, &requirements);
  auto type = FindMemoryType(context, requirements.memoryTypeBits, required, preferred);
  if (!type) { return std::unexpected(type.error()); }
  VkMemoryDedicatedAllocateInfo dedicated{};
  dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicated.buffer = buffer;
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.pNext = &dedicated;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = *type;
  result = context.disp.allocateMemory(&allocation, nullptr, &memory);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate video buffer", result)); }
  result = context.disp.bindBufferMemory(buffer, memory, 0);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("bind video buffer", result)); }
  return {};
}

[[nodiscard]] auto CreateCommandPool(Context &context, std::uint32_t family,
  VkCommandPool &pool) -> std::expected<void, Error>
{
  VkCommandPoolCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  create.queueFamilyIndex = family;
  create.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  auto const result = context.disp.createCommandPool(&create, nullptr, &pool);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create video command pool", result)); }
  return {};
}

[[nodiscard]] auto AllocateCommand(Context &context, VkCommandPool pool, VkCommandBuffer &command)
  -> std::expected<void, Error>
{
  VkCommandBufferAllocateInfo allocate{};
  allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate.commandPool = pool;
  allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate.commandBufferCount = kOneResource;
  auto const result = context.disp.allocateCommandBuffers(&allocate, &command);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate video command buffer", result)); }
  return {};
}

[[nodiscard]] auto BeginCommand(Context &context, VkCommandBuffer command) -> std::expected<void, Error>
{
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  auto const result = context.disp.beginCommandBuffer(command, &begin);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("begin video command buffer", result)); }
  return {};
}

[[nodiscard]] auto EndCommand(Context &context, VkCommandBuffer command) -> std::expected<void, Error>
{
  auto const result = context.disp.endCommandBuffer(command);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("end video command buffer", result)); }
  return {};
}

[[nodiscard]] auto Submit(Context &context, VkQueue queue, VkCommandBuffer command,
  VkSemaphore wait, VkPipelineStageFlags2 wait_stage,
  VkSemaphore signal, VkFence fence) -> std::expected<void, Error>
{
  VkCommandBufferSubmitInfo command_info{};
  command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  command_info.commandBuffer = command;
  VkSemaphoreSubmitInfo wait_info{};
  wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  wait_info.semaphore = wait;
  wait_info.stageMask = wait_stage;
  VkSemaphoreSubmitInfo signal_info{};
  signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signal_info.semaphore = signal;
  signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkSubmitInfo2 submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submit.waitSemaphoreInfoCount = wait == VK_NULL_HANDLE ? 0 : kOneResource;
  submit.pWaitSemaphoreInfos = wait == VK_NULL_HANDLE ? nullptr : &wait_info;
  submit.commandBufferInfoCount = kOneResource;
  submit.pCommandBufferInfos = &command_info;
  submit.signalSemaphoreInfoCount = signal == VK_NULL_HANDLE ? 0 : kOneResource;
  submit.pSignalSemaphoreInfos = signal == VK_NULL_HANDLE ? nullptr : &signal_info;
  auto const queue_lock = context.vkexec_context->lock_queue(queue);
  auto const result = context.disp.queueSubmit2(queue, kOneResource, &submit, fence);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("submit video command buffer", result)); }
  return {};
}

[[nodiscard]] auto CreateEncoderResources(Context &context, EncodeDevice const &device,
  VkVideoProfileListInfoKHR const &profiles, EncoderResources &resources, VkExtent2D extent,
  VkDeviceSize upload_size, VkDeviceSize bitstream_size,
  std::uint32_t graphics_family) -> std::expected<void, Error>
{
  auto const &format = device.selection.source_format;
  std::array<std::uint32_t, 2> const families{ graphics_family, device.queue_family_index };
  auto const sharing = graphics_family == device.queue_family_index
                       ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
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
    .queueFamilyIndexCount = sharing == VK_SHARING_MODE_CONCURRENT
                               ? static_cast<std::uint32_t>(families.size()) : 0,
    .pQueueFamilyIndices = sharing == VK_SHARING_MODE_CONCURRENT ? families.data() : nullptr,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  auto result = context.disp.createImage(&image, nullptr, &resources.image);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create NV12 encode image", result)); }
  if (auto allocated = AllocateImageMemory(context, resources); !allocated) { return allocated; }

  VkImageViewCreateInfo view{};
  view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view.image = resources.image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = format.format;
  view.components = format.components;
  view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view.subresourceRange.levelCount = kOneResource;
  view.subresourceRange.layerCount = kOneResource;
  result = context.disp.createImageView(&view, nullptr, &resources.view);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create NV12 encode image view", result)); }

  if (auto created = CreateBuffer(context, upload_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        nullptr, VK_SHARING_MODE_EXCLUSIVE, {},
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0,
        resources.upload, resources.upload_memory); !created) { return created; }
  if (auto created = CreateBuffer(context, bitstream_size,
        VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        &profiles, sharing, families, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        resources.bitstream, resources.bitstream_memory); !created) { return created; }
  if (auto created = CreateBuffer(context, bitstream_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        nullptr, VK_SHARING_MODE_EXCLUSIVE, {},
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0,
        resources.readback, resources.readback_memory); !created) { return created; }

  if (auto created = CreateCommandPool(context, graphics_family, resources.upload_pool); !created) { return created; }
  if (auto created = CreateCommandPool(context, device.queue_family_index, resources.video_pool); !created) {
    return created;
  }
  if (auto allocated = AllocateCommand(context, resources.upload_pool, resources.upload_cmd); !allocated) {
    return allocated;
  }
  if (auto allocated = AllocateCommand(context, resources.video_pool, resources.video_cmd); !allocated) {
    return allocated;
  }
  if (auto allocated = AllocateCommand(context, resources.upload_pool, resources.readback_cmd); !allocated) {
    return allocated;
  }
  VkSemaphoreCreateInfo semaphore{};
  semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  result = context.disp.createSemaphore(&semaphore, nullptr, &resources.upload_done);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create upload semaphore", result)); }
  result = context.disp.createSemaphore(&semaphore, nullptr, &resources.video_done);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create video semaphore", result)); }
  VkFenceCreateInfo fence{};
  fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  result = context.disp.createFence(&fence, nullptr, &resources.readback_done);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create video fence", result)); }
  VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedback{};
  feedback.sType = VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR;
  feedback.pNext = profiles.pProfiles;
  feedback.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR
                                 | VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;
  VkQueryPoolCreateInfo pool{};
  pool.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  pool.pNext = &feedback;
  pool.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
  pool.queryCount = kOneResource;
  result = context.disp.createQueryPool(&pool, nullptr, &resources.feedback);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create encode feedback query", result)); }
  return {};
}

[[nodiscard]] auto RecordUpload(Context &context, EncoderResources &resources,
  VkDeviceSize y_size, VkExtent2D extent) -> std::expected<void, Error>
{
  auto result = BeginCommand(context, resources.upload_cmd);
  if (!result) { return result; }
  VkImageMemoryBarrier2 to_copy{};
  to_copy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  to_copy.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  to_copy.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  to_copy.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_copy.image = resources.image;
  to_copy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  to_copy.subresourceRange.levelCount = kOneResource;
  to_copy.subresourceRange.layerCount = kOneResource;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = kOneResource;
  dependency.pImageMemoryBarriers = &to_copy;
  context.disp.cmdPipelineBarrier2(resources.upload_cmd, &dependency);

  std::array<VkBufferImageCopy, 2> regions{};
  auto &luma = regions.at(kLumaPlaneIndex);
  luma.bufferOffset = 0;
  luma.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
  luma.imageSubresource.layerCount = kOneResource;
  luma.imageExtent = { .width = extent.width, .height = extent.height, .depth = kOneResource };
  auto &chroma = regions.at(kChromaPlaneIndex);
  chroma.bufferOffset = y_size;
  chroma.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
  chroma.imageSubresource.layerCount = kOneResource;
  chroma.imageExtent = {
    .width = extent.width / static_cast<std::uint32_t>(kChromaSubsamplingDivisor),
    .height = extent.height / static_cast<std::uint32_t>(kChromaSubsamplingDivisor), .depth = kOneResource };
  context.disp.cmdCopyBufferToImage(resources.upload_cmd, resources.upload, resources.image,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(regions.size()), regions.data());

  VkImageMemoryBarrier2 to_encode{};
  to_encode.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  to_encode.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  to_encode.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  to_encode.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_encode.newLayout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
  to_encode.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_encode.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_encode.image = resources.image;
  to_encode.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  to_encode.subresourceRange.levelCount = kOneResource;
  to_encode.subresourceRange.layerCount = kOneResource;
  dependency.pImageMemoryBarriers = &to_encode;
  context.disp.cmdPipelineBarrier2(resources.upload_cmd, &dependency);
  return EndCommand(context, resources.upload_cmd);
}

[[nodiscard]] auto RecordEncode(Context &context, EncoderResources &resources, VideoSession const &session,
  EncodeDevice const &device, VkDeviceSize bitstream_size, VkExtent2D extent,
  VkImageView source_view, bool has_prior_submission) -> std::expected<void, Error>
{
  auto result = BeginCommand(context, resources.video_cmd);
  if (!result) { return result; }
  context.disp.cmdResetQueryPool(resources.video_cmd, resources.feedback, 0, kOneResource);
  VkVideoEncodeRateControlInfoKHR rate{};
  rate.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_INFO_KHR;
  rate.rateControlMode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
  VkVideoBeginCodingInfoKHR begin{};
  begin.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
  begin.pNext = has_prior_submission ? &rate : nullptr;
  begin.videoSession = session.handle();
  begin.videoSessionParameters = session.parameters();
  context.disp.cmdBeginVideoCodingKHR(resources.video_cmd, &begin);
  VkVideoCodingControlInfoKHR reset{};
  reset.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
  reset.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
  context.disp.cmdControlVideoCodingKHR(resources.video_cmd, &reset);
  VkVideoCodingControlInfoKHR control{};
  control.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
  control.pNext = &rate;
  control.flags = VK_VIDEO_CODING_CONTROL_ENCODE_RATE_CONTROL_BIT_KHR;
  context.disp.cmdControlVideoCodingKHR(resources.video_cmd, &control);

  auto const &profile = device.capabilities.profiles.at(device.selection.profile_index);
  auto const quantization_parameter = std::clamp(kPreferredQp, profile.h264.minQp, profile.h264.maxQp);
  StdVideoEncodeH264SliceHeader slice{};
  slice.slice_type = STD_VIDEO_H264_SLICE_TYPE_I;
  VkVideoEncodeH264NaluSliceInfoKHR nalu{};
  nalu.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_NALU_SLICE_INFO_KHR;
  nalu.constantQp = quantization_parameter;
  nalu.pStdSliceHeader = &slice;
  StdVideoEncodeH264PictureInfo picture{};
  picture.flags.IdrPicFlag = 1;
  picture.flags.is_reference = 1;
  picture.primary_pic_type = STD_VIDEO_H264_PICTURE_TYPE_IDR;
  VkVideoEncodeH264PictureInfoKHR h264{};
  h264.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PICTURE_INFO_KHR;
  h264.naluSliceEntryCount = kOneResource;
  h264.pNaluSliceEntries = &nalu;
  h264.pStdPictureInfo = &picture;
  VkVideoEncodeInfoKHR encode{};
  encode.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR;
  encode.pNext = &h264;
  encode.dstBuffer = resources.bitstream;
  encode.dstBufferRange = bitstream_size;
  encode.srcPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
  encode.srcPictureResource.codedExtent = extent;
  encode.srcPictureResource.imageViewBinding = source_view;
  context.disp.cmdBeginQuery(resources.video_cmd, resources.feedback, 0, 0);
  context.disp.cmdEncodeVideoKHR(resources.video_cmd, &encode);
  context.disp.cmdEndQuery(resources.video_cmd, resources.feedback, 0);
  VkVideoEndCodingInfoKHR end{};
  end.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;
  context.disp.cmdEndVideoCodingKHR(resources.video_cmd, &end);
  return EndCommand(context, resources.video_cmd);
}

[[nodiscard]] auto RecordReadback(Context &context, EncoderResources &resources,
  VkDeviceSize bitstream_size) -> std::expected<void, Error>
{
  auto result = BeginCommand(context, resources.readback_cmd);
  if (!result) { return result; }
  VkBufferCopy copy{};
  copy.size = bitstream_size;
  context.disp.cmdCopyBuffer(resources.readback_cmd, resources.bitstream, resources.readback,
    kOneResource, &copy);
  return EndCommand(context, resources.readback_cmd);
}

[[nodiscard]] auto InitializeEncoderResources(Context &context, EncodeDevice const &device,
  VkExtent2D extent, EncoderResources &resources) -> std::expected<void, Error>
{
  auto graphics = context.device.get_queue_and_index(vkb::QueueType::graphics);
  if (!graphics) {
    return std::unexpected(vkgsplat::MakeError(std::errc::function_not_supported,
      "graphics queue unavailable for NV12 upload"));
  }
  auto const y_size = static_cast<VkDeviceSize>(extent.width) * extent.height;
  auto const upload_size = y_size + (y_size / kChromaSubsamplingDivisor);
  auto const &capabilities = device.capabilities.profiles.at(device.selection.profile_index).capabilities;
  auto const alignment = std::max<VkDeviceSize>(kOneResource, capabilities.minBitstreamBufferSizeAlignment);
  auto const requested = y_size * kBitstreamBytesPerPixel;
  auto const bitstream_size = ((requested + alignment - 1) / alignment) * alignment;
  VkVideoEncodeH264ProfileInfoKHR h264_profile{
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
    .pNext = nullptr,
    .stdProfileIdc = device.selection.profile,
  };
  VkVideoProfileInfoKHR profile{};
  profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
  profile.pNext = &h264_profile;
  profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
  profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
  profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
  profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
  VkVideoProfileListInfoKHR profiles{};
  profiles.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
  profiles.profileCount = kOneResource;
  profiles.pProfiles = &profile;
  if (auto created = CreateEncoderResources(context, device, profiles, resources, extent,
        upload_size, bitstream_size, graphics->second); !created) {
    return std::unexpected(created.error());
  }
  resources.graphics_queue = graphics->first;
  resources.video_queue = device.queue;
  resources.upload_size = upload_size;
  resources.bitstream_size = bitstream_size;
  return {};
}

[[nodiscard]] auto SubmitPicture(Context &context, EncodeDevice const &device,
  VideoSession const &session, VkExtent2D extent, VkImageView source_view,
  VkSemaphore ready_semaphore, std::span<std::byte const> frame,
  EncoderResources &resources, EncodeFailurePointForTest failure_point,
  bool has_prior_submission)
  -> std::expected<void, Error>
{
  auto const y_size = static_cast<VkDeviceSize>(extent.width) * extent.height;
  auto const upload_size = resources.upload_size;
  auto const bitstream_size = resources.bitstream_size;
  for (auto *command : { resources.upload_cmd, resources.video_cmd, resources.readback_cmd }) {
    auto const result = context.disp.resetCommandBuffer(command, 0);
    if (result != VK_SUCCESS) { return std::unexpected(Failure("reset video command buffer", result)); }
  }
  auto const reset = context.disp.resetFences(kOneResource, &resources.readback_done);
  if (reset != VK_SUCCESS) { return std::unexpected(Failure("reset video fence", reset)); }
  auto const upload = ready_semaphore == VK_NULL_HANDLE;
  if (upload) {
    void *mapped = nullptr;
    auto const mapped_result = context.disp.mapMemory(resources.upload_memory, 0, upload_size, 0, &mapped);
    if (mapped_result != VK_SUCCESS) { return std::unexpected(Failure("map NV12 upload", mapped_result)); }
    std::memcpy(mapped, frame.data(), frame.size());
    context.disp.unmapMemory(resources.upload_memory);
    if (auto recorded = RecordUpload(context, resources, y_size, extent); !recorded) {
      return std::unexpected(recorded.error());
    }
  }
  if (auto recorded = RecordEncode(context, resources, session, device, bitstream_size, extent,
        source_view, has_prior_submission); !recorded) {
    return std::unexpected(recorded.error());
  }
  if (auto recorded = RecordReadback(context, resources, bitstream_size); !recorded) {
    return std::unexpected(recorded.error());
  }
  if (upload) {
    if (auto submitted = Submit(context, resources.graphics_queue, resources.upload_cmd,
          VK_NULL_HANDLE, 0, resources.upload_done, VK_NULL_HANDLE); !submitted) {
      return std::unexpected(submitted.error());
    }
    resources.in_flight = true;
    ready_semaphore = resources.upload_done;
  }
  if (failure_point == EncodeFailurePointForTest::kVideoSubmission) {
    return std::unexpected(MakeError(std::errc::io_error, "injected video submission failure"));
  }
  if (auto submitted = Submit(context, resources.video_queue, resources.video_cmd,
        ready_semaphore, VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
        resources.video_done, VK_NULL_HANDLE); !submitted) {
    return std::unexpected(submitted.error());
  }
  resources.in_flight = true;
  if (failure_point == EncodeFailurePointForTest::kReadbackSubmission) {
    return std::unexpected(MakeError(std::errc::io_error, "injected bitstream readback submission failure"));
  }
  if (auto submitted = Submit(context, resources.graphics_queue, resources.readback_cmd,
        resources.video_done, VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_NULL_HANDLE, resources.readback_done); !submitted) {
    return std::unexpected(submitted.error());
  }
  return {};
}

[[nodiscard]] auto ReadPicture(Context &context, EncoderResources &resources)
  -> std::expected<std::vector<std::byte>, Error>
{
  auto const bitstream_size = resources.bitstream_size;
  resources.in_flight = false;
  struct Feedback
  {
    std::uint32_t offset{};
    std::uint32_t size{};
    VkQueryResultStatusKHR status{};
  } feedback{};
  auto result = context.disp.getQueryPoolResults(resources.feedback, 0, kOneResource,
    sizeof(feedback), &feedback, sizeof(feedback), VK_QUERY_RESULT_WITH_STATUS_BIT_KHR);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("read encode feedback", result)); }
  if (feedback.status != VK_QUERY_RESULT_STATUS_COMPLETE_KHR || feedback.size == 0
      || feedback.offset > bitstream_size || feedback.size > bitstream_size - feedback.offset) {
    return std::unexpected(vkgsplat::MakeError(std::errc::io_error, "invalid encoded bitstream feedback"));
  }
  void *mapped = nullptr;
  result = context.disp.mapMemory(resources.readback_memory, 0, bitstream_size, 0, &mapped);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("map encoded picture", result)); }
  auto const mapped_bytes = std::span{ static_cast<std::byte const *>(mapped),
    static_cast<std::size_t>(bitstream_size) };
  auto const encoded_bytes = mapped_bytes.subspan(feedback.offset, feedback.size);
  std::vector<std::byte> picture(encoded_bytes.begin(), encoded_bytes.end());
  context.disp.unmapMemory(resources.readback_memory);
  if (picture.size() < kAnnexBStartCode.size()
      || !std::equal(kAnnexBStartCode.begin(), kAnnexBStartCode.end(), picture.begin())) {
    constexpr std::array kShortStartCode{ std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 } };
    if (picture.size() < kShortStartCode.size()
        || !std::equal(kShortStartCode.begin(), kShortStartCode.end(), picture.begin())) {
      // One slice is requested, so an implementation that emits a bare NAL
      // can be framed as one Annex B access unit.
      picture.insert(picture.begin(), kAnnexBStartCode.begin(), kAnnexBStartCode.end());
    }
  }
  return picture;
}

[[nodiscard]] auto CompletePicture(Context &context, EncoderResources &resources)
  -> std::expected<std::vector<std::byte>, Error>
{
  auto const result = context.disp.waitForFences(kOneResource, &resources.readback_done, VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("wait for encoded picture", result)); }
  return ReadPicture(context, resources);
}

}// namespace

VideoEncoder::VideoEncoder(Context &context, EncodeDevice device, VkExtent2D extent,
  std::unique_ptr<VideoSession> session) noexcept
  : context_{ context }, device_{ std::move(device) }, extent_{ extent }, session_{ std::move(session) }
{}

VideoEncoder::~VideoEncoder() = default;

auto VideoEncoder::Create(Context &context, EncodeDevice device, VkExtent2D extent,
  std::uint32_t fps_numerator, std::uint32_t fps_denominator, std::size_t max_in_flight_frames,
  bool fixed_frame_rate)
  -> std::expected<std::unique_ptr<VideoEncoder>, Error>
{
  if (max_in_flight_frames == 0U || max_in_flight_frames > kMaximumInFlightFrames) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 resource slot count"));
  }
  if (device.selection.profile_index >= device.capabilities.profiles.size()) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid H.264 encode profile selection"));
  }
  if (device.selection.source_format.format != VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
      || device.selection.upload_strategy != UploadStrategy::kTransferDestination
      || !device.selection.source_format.transfer_destination) {
    return std::unexpected(MakeError(std::errc::function_not_supported,
      "H.264 encoder requires an NV12 encode source with transfer-destination support"));
  }
  auto session = VideoSession::Create(context, device, extent, fps_numerator, fps_denominator, fixed_frame_rate);
  if (!session) { return std::unexpected(session.error()); }
  auto encoder = std::unique_ptr<VideoEncoder>{ new VideoEncoder{ context, std::move(device), extent, std::move(*session) } };
  encoder->resources_.reserve(max_in_flight_frames);
  encoder->source_owners_.resize(max_in_flight_frames);
  encoder->pending_metadata_.resize(max_in_flight_frames);
  for (std::size_t slot = 0U; slot < max_in_flight_frames; ++slot) {
    auto resources = std::make_unique<EncoderResources>(context);
    if (auto created = InitializeEncoderResources(context, encoder->device_, extent, *resources); !created) {
      return std::unexpected(created.error());
    }
    encoder->resources_.push_back(std::move(resources));
  }
  return encoder;
}

auto VideoEncoder::EncodeNv12(std::span<std::byte const> frame, std::uint64_t frame_index)
  -> std::expected<EncodedPacket, Error>
{
  auto const y_size = static_cast<std::uint64_t>(extent_.width) * extent_.height;
  auto const expected_size = y_size + (y_size / kChromaSubsamplingDivisor);
  if (frame.size() != expected_size) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "NV12 frame size does not match coded extent"));
  }
  return EncodeSource(resources_.front()->view, VK_NULL_HANDLE, frame, frame_index);
}

auto VideoEncoder::EncodeImage(PreparedVideoFrame const &frame, std::uint64_t frame_index)
  -> std::expected<EncodedPacket, Error>
{
  if (next_submit_index_ != next_index_) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted,
      "synchronous H.264 encoding requires all asynchronous pictures to be completed"));
  }
  if (frame_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return std::unexpected(MakeError(std::errc::value_too_large, "H.264 frame index exceeds timestamp range"));
  }
  auto const timestamp = static_cast<std::int64_t>(frame_index);
  if (auto submitted = SubmitImageImpl(frame, PictureMetadata{
        .encode_index = frame_index,
        .presentation_timestamp = timestamp,
      }, {}, false); !submitted) { return std::unexpected(submitted.error()); }
  return CompleteImage();
}

auto VideoEncoder::SubmitImage(PreparedVideoFrame const &frame, PictureMetadata metadata,
  std::shared_ptr<void> source_owner)
  -> std::expected<void, Error>
{
  return SubmitImageImpl(frame, metadata, std::move(source_owner), true);
}

auto VideoEncoder::SubmitImageImpl(PreparedVideoFrame const &frame, PictureMetadata metadata,
  std::shared_ptr<void> source_owner, bool require_owner)
  -> std::expected<void, Error>
{
  if (require_owner && !source_owner) {
    return std::unexpected(MakeError(std::errc::invalid_argument,
      "asynchronous H.264 submission requires a source owner"));
  }
  if (frame.image_view == VK_NULL_HANDLE || frame.ready_binary_semaphore == VK_NULL_HANDLE
      || frame.format != device_.selection.source_format.format
      || frame.layout != VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR
      || frame.extent.width != extent_.width || frame.extent.height != extent_.height) {
    return std::unexpected(MakeError(std::errc::invalid_argument,
      "prepared video frame requires a matching NV12 image, encode-source layout and ready semaphore"));
  }
  if (failed_ || finished_ || metadata.encode_index != next_submit_index_
      || metadata.encode_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
      || metadata.presentation_timestamp < next_minimum_timestamp_ || metadata.duration <= 0
      || metadata.presentation_timestamp > std::numeric_limits<std::int64_t>::max() - metadata.duration) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted,
      "H.264 encoder cannot accept this picture"));
  }
  if (next_submit_index_ - next_index_ >= resources_.size()) {
    return std::unexpected(MakeError(std::errc::resource_unavailable_try_again,
      "all H.264 encoding resource slots are busy"));
  }
  auto const slot = static_cast<std::size_t>(metadata.encode_index % resources_.size());
  auto &resources = *resources_.at(slot);
  source_owners_.at(slot) = std::move(source_owner);
  auto submitted = SubmitPicture(context_, device_, *session_, extent_, frame.image_view,
    frame.ready_binary_semaphore, {}, resources, failure_point_for_test_, next_submit_index_ != 0U);
  failure_point_for_test_ = EncodeFailurePointForTest::kNone;
  if (!submitted) {
    failed_ = true;
    return std::unexpected(submitted.error());
  }
  pending_metadata_.at(slot) = metadata;
  next_minimum_timestamp_ = metadata.presentation_timestamp + metadata.duration;
  ++next_submit_index_;
  return {};
}

auto VideoEncoder::CompleteImage() -> std::expected<EncodedPacket, Error>
{
  if (next_index_ == next_submit_index_ || failed_) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted, "no H.264 picture is pending"));
  }
  auto &resources = *resources_.at(static_cast<std::size_t>(next_index_ % resources_.size()));
  auto const waited = context_.disp.waitForFences(kOneResource, &resources.readback_done, VK_TRUE, UINT64_MAX);
  if (waited != VK_SUCCESS) {
    failed_ = true;
    return std::unexpected(Failure("wait for encoded picture", waited));
  }
  return CompleteReadyImage();
}

auto VideoEncoder::TryCompleteImage() -> std::expected<std::optional<EncodedPacket>, Error>
{
  if (next_index_ == next_submit_index_ || failed_) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted, "no H.264 picture is pending"));
  }
  auto &resources = *resources_.at(static_cast<std::size_t>(next_index_ % resources_.size()));
  auto const status = context_.disp.getFenceStatus(resources.readback_done);
  if (status == VK_NOT_READY) { return std::nullopt; }
  if (status != VK_SUCCESS) {
    failed_ = true;
    return std::unexpected(Failure("poll encoded picture", status));
  }
  auto completed = CompleteReadyImage();
  if (!completed) { return std::unexpected(completed.error()); }
  return std::optional<EncodedPacket>{ std::move(*completed) };
}

auto VideoEncoder::CompleteReadyImage() -> std::expected<EncodedPacket, Error>
{
  auto const slot = static_cast<std::size_t>(next_index_ % resources_.size());
  auto &resources = *resources_.at(slot);
  auto picture = ReadPicture(context_, resources);
  if (!picture) {
    failed_ = true;
    return std::unexpected(picture.error());
  }
  auto const &pending_metadata = pending_metadata_.at(slot);
  if (!pending_metadata) {
    failed_ = true;
    return std::unexpected(vkgsplat::MakeError(std::errc::state_not_recoverable,
      "completed video picture has no pending metadata"));
  }
  auto const metadata = *pending_metadata;
  pending_metadata_.at(slot).reset();
  source_owners_.at(slot).reset();
  EncodedPacket packet{
    .data = std::move(*picture),
    .frame_index = metadata.encode_index,
    .pts = metadata.presentation_timestamp,
    .dts = metadata.presentation_timestamp,
    .duration = metadata.duration,
    .keyframe = true,
  };
  ++next_index_;
  return packet;
}

auto VideoEncoder::WaitForInFlightWork() -> std::expected<void, Error>
{
  bool in_flight = false;
  for (auto const &resources : resources_) { in_flight = in_flight || resources->in_flight; }
  if (!in_flight) { return {}; }
  auto const &first = *resources_.front();
  for (auto *queue : { first.graphics_queue, first.video_queue }) {
    if (queue == VK_NULL_HANDLE) { continue; }
    auto const queue_lock = context_.vkexec_context->lock_queue(queue);
    auto const result = context_.disp.queueWaitIdle(queue);
    if (result != VK_SUCCESS) { return std::unexpected(Failure("wait for video encode work", result)); }
    if (queue == first.video_queue && first.video_queue == first.graphics_queue) { break; }
  }
  for (auto const &resources : resources_) { resources->in_flight = false; }
  for (auto &owner : source_owners_) { owner.reset(); }
  return {};
}

auto VideoEncoder::EncodeSource(VkImageView source_view, VkSemaphore ready_semaphore,
  std::span<std::byte const> frame, std::uint64_t frame_index) -> std::expected<EncodedPacket, Error>
{
  if (failed_) {
    return std::unexpected(MakeError(std::errc::state_not_recoverable,
      "H.264 encoder failed; create a new recording session"));
  }
  if (finished_ || next_submit_index_ != next_index_ || frame_index != next_index_
      || frame_index >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid or out-of-order H.264 frame"));
  }
  auto submitted = SubmitPicture(context_, device_, *session_, extent_, source_view,
    ready_semaphore, frame, *resources_.front(), failure_point_for_test_, next_submit_index_ != 0U);
  failure_point_for_test_ = EncodeFailurePointForTest::kNone;
  if (!submitted) {
    failed_ = true;
    return std::unexpected(submitted.error());
  }
  auto picture = CompletePicture(context_, *resources_.front());
  if (!picture) {
    failed_ = true;
    return std::unexpected(picture.error());
  }
  EncodedPacket packet{
    .data = std::move(*picture),
    .frame_index = frame_index,
    .pts = static_cast<std::int64_t>(frame_index),
    .dts = static_cast<std::int64_t>(frame_index),
    .duration = 1,
    .keyframe = true,
  };
  ++next_index_;
  ++next_submit_index_;
  next_minimum_timestamp_ = packet.pts + packet.duration;
  return packet;
}

auto VideoEncoder::Finish() -> std::expected<void, Error>
{
  if (failed_) {
    return std::unexpected(MakeError(std::errc::state_not_recoverable,
      "H.264 encoder failed; create a new recording session"));
  }
  if (finished_ || next_submit_index_ != next_index_) {
    return std::unexpected(MakeError(std::errc::operation_not_permitted, "H.264 encoder is finalized"));
  }
  finished_ = true;
  return {};
}

auto VideoEncoder::ParameterSets() const noexcept -> std::span<std::byte const>
{
  return session_->parameter_sets();
}

}// namespace vkgsplat::vulkan::video
