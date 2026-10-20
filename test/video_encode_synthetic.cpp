#include "../src/vkgsplat_backend/vulkan/video/video_session.hpp"
#include "../src/vkgsplat_backend/vulkan/vulkan_driver.hpp"
#include "../src/vkgsplat_io/video/h264_writer.hpp"

#include <vkgsplat_headless/offscreen_platform.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using vkgsplat::Error;
using vkgsplat::vulkan::Context;
using vkgsplat::vulkan::video::EncodeDevice;
using vkgsplat::vulkan::video::VideoSession;

constexpr int kSkipped = 77;
constexpr VkExtent2D kExtent{ .width = 1280, .height = 720 };
constexpr std::uint8_t kNeutralLuma = 128;
constexpr std::uint8_t kNeutralChroma = 128;
constexpr std::int32_t kPreferredQp = 26;
constexpr VkDeviceSize kBitstreamBytesPerPixel = 2;
constexpr VkDeviceSize kChromaSubsamplingDivisor = 2;
constexpr std::uint32_t kOneResource = 1;
constexpr size_t kLumaPlaneIndex = 0;
constexpr size_t kChromaPlaneIndex = 1;
constexpr std::array kAnnexBStartCode{
  std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 },
};
constexpr std::string_view kUnsupportedDeviceMessage =
  "No rendering device supports required 8-bit 4:2:0 H.264 encoding and image formats.";

[[nodiscard]] auto RequireVideoEncode() -> bool
{
  auto const *value = std::getenv("VKGSPLAT_TEST_REQUIRE_VIDEO_ENCODE");
  return value != nullptr && std::string_view{ value } == "1";
}

auto PrintSelectionDetails(EncodeDevice const &device) -> void
{
  std::cout << "Device: " << device.capabilities.device_name << '\n';
  std::cout << "Capabilities: " << device.capabilities.diagnostic() << '\n';
  for (auto const &profile : device.capabilities.profiles) {
    std::cout << "H.264 profile " << static_cast<int>(profile.profile) << " source formats:";
    for (auto const &format : profile.source_formats) {
      std::cout << ' ' << static_cast<int>(format.format)
                << "(transfer destination=" << format.transfer_destination << ')';
    }
    std::cout << '\n';
  }
}

[[nodiscard]] auto Failure(char const *operation, VkResult result) -> Error
{
  return vkgsplat::MakeError(std::errc::io_error,
    std::string{ operation } + " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
}

struct Resources
{
  Context &context;
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

  explicit Resources(Context &value) : context{ value } {}

  ~Resources()
  {
    context.disp.deviceWaitIdle();
    if (feedback != VK_NULL_HANDLE) { context.disp.destroyQueryPool(feedback, nullptr); }
    if (readback_done != VK_NULL_HANDLE) { context.disp.destroyFence(readback_done, nullptr); }
    if (video_done != VK_NULL_HANDLE) { context.disp.destroySemaphore(video_done, nullptr); }
    if (upload_done != VK_NULL_HANDLE) { context.disp.destroySemaphore(upload_done, nullptr); }
    if (video_pool != VK_NULL_HANDLE) { context.disp.destroyCommandPool(video_pool, nullptr); }
    if (upload_pool != VK_NULL_HANDLE) { context.disp.destroyCommandPool(upload_pool, nullptr); }
    if (readback != VK_NULL_HANDLE) { context.disp.destroyBuffer(readback, nullptr); }
    if (bitstream != VK_NULL_HANDLE) { context.disp.destroyBuffer(bitstream, nullptr); }
    if (upload != VK_NULL_HANDLE) { context.disp.destroyBuffer(upload, nullptr); }
    if (view != VK_NULL_HANDLE) { context.disp.destroyImageView(view, nullptr); }
    if (image != VK_NULL_HANDLE) { context.disp.destroyImage(image, nullptr); }
    if (readback_memory != VK_NULL_HANDLE) { context.disp.freeMemory(readback_memory, nullptr); }
    if (bitstream_memory != VK_NULL_HANDLE) { context.disp.freeMemory(bitstream_memory, nullptr); }
    if (upload_memory != VK_NULL_HANDLE) { context.disp.freeMemory(upload_memory, nullptr); }
    if (image_memory != VK_NULL_HANDLE) { context.disp.freeMemory(image_memory, nullptr); }
  }
};

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

[[nodiscard]] auto AllocateImageMemory(Context &context, Resources &resources) -> std::expected<void, Error>
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
  auto const result = context.disp.queueSubmit2(queue, kOneResource, &submit, fence);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("submit video command buffer", result)); }
  return {};
}

[[nodiscard]] auto CreateResources(Context &context, EncodeDevice const &device,
  VkVideoProfileListInfoKHR const &profiles, Resources &resources,
  VkDeviceSize upload_size, VkDeviceSize bitstream_size,
  std::uint32_t graphics_family) -> std::expected<void, Error>
{
  auto const &format = device.selection.source_format;
  std::array<std::uint32_t, 2> const families{ graphics_family, device.queue_family_index };
  auto const sharing = graphics_family == device.queue_family_index
                       ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  VkImageCreateInfo image{};
  image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image.pNext = &profiles;
  image.flags = 0;
  image.imageType = format.image_type;
  image.format = format.format;
  image.extent = { kExtent.width, kExtent.height, kOneResource };
  image.mipLevels = kOneResource;
  image.arrayLayers = kOneResource;
  image.samples = VK_SAMPLE_COUNT_1_BIT;
  image.tiling = format.tiling;
  image.usage = VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image.sharingMode = sharing;
  image.queueFamilyIndexCount = sharing == VK_SHARING_MODE_CONCURRENT
                                 ? static_cast<std::uint32_t>(families.size()) : 0;
  image.pQueueFamilyIndices = sharing == VK_SHARING_MODE_CONCURRENT ? families.data() : nullptr;
  image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

[[nodiscard]] auto RecordUpload(Context &context, Resources &resources,
  VkDeviceSize y_size) -> std::expected<void, Error>
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
  regions[kLumaPlaneIndex].bufferOffset = 0;
  regions[kLumaPlaneIndex].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
  regions[kLumaPlaneIndex].imageSubresource.layerCount = kOneResource;
  regions[kLumaPlaneIndex].imageExtent = { kExtent.width, kExtent.height, kOneResource };
  regions[kChromaPlaneIndex].bufferOffset = y_size;
  regions[kChromaPlaneIndex].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
  regions[kChromaPlaneIndex].imageSubresource.layerCount = kOneResource;
  regions[kChromaPlaneIndex].imageExtent = {
    kExtent.width / static_cast<std::uint32_t>(kChromaSubsamplingDivisor),
    kExtent.height / static_cast<std::uint32_t>(kChromaSubsamplingDivisor), kOneResource };
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

[[nodiscard]] auto RecordEncode(Context &context, Resources &resources, VideoSession const &session,
  EncodeDevice const &device, VkDeviceSize bitstream_size) -> std::expected<void, Error>
{
  auto result = BeginCommand(context, resources.video_cmd);
  if (!result) { return result; }
  context.disp.cmdResetQueryPool(resources.video_cmd, resources.feedback, 0, kOneResource);
  VkVideoBeginCodingInfoKHR begin{};
  begin.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
  begin.videoSession = session.handle();
  begin.videoSessionParameters = session.parameters();
  context.disp.cmdBeginVideoCodingKHR(resources.video_cmd, &begin);
  VkVideoCodingControlInfoKHR reset{};
  reset.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
  reset.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
  context.disp.cmdControlVideoCodingKHR(resources.video_cmd, &reset);
  VkVideoEncodeRateControlInfoKHR rate{};
  rate.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_INFO_KHR;
  rate.rateControlMode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
  VkVideoCodingControlInfoKHR control{};
  control.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
  control.pNext = &rate;
  control.flags = VK_VIDEO_CODING_CONTROL_ENCODE_RATE_CONTROL_BIT_KHR;
  context.disp.cmdControlVideoCodingKHR(resources.video_cmd, &control);

  auto const &profile = device.capabilities.profiles[device.selection.profile_index];
  auto const qp = std::clamp(kPreferredQp, profile.h264.minQp, profile.h264.maxQp);
  StdVideoEncodeH264SliceHeader slice{};
  slice.slice_type = STD_VIDEO_H264_SLICE_TYPE_I;
  VkVideoEncodeH264NaluSliceInfoKHR nalu{};
  nalu.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_NALU_SLICE_INFO_KHR;
  nalu.constantQp = qp;
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
  encode.srcPictureResource.codedExtent = kExtent;
  encode.srcPictureResource.imageViewBinding = resources.view;
  context.disp.cmdBeginQuery(resources.video_cmd, resources.feedback, 0, 0);
  context.disp.cmdEncodeVideoKHR(resources.video_cmd, &encode);
  context.disp.cmdEndQuery(resources.video_cmd, resources.feedback, 0);
  VkVideoEndCodingInfoKHR end{};
  end.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;
  context.disp.cmdEndVideoCodingKHR(resources.video_cmd, &end);
  return EndCommand(context, resources.video_cmd);
}

[[nodiscard]] auto RecordReadback(Context &context, Resources &resources,
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

[[nodiscard]] auto EncodeSynthetic(Context &context, EncodeDevice const &device,
  VideoSession const &session) -> std::expected<std::vector<std::byte>, Error>
{
  auto graphics = context.device.get_queue_and_index(vkb::QueueType::graphics);
  if (!graphics) {
    return std::unexpected(vkgsplat::MakeError(std::errc::function_not_supported,
      "graphics queue unavailable for NV12 upload"));
  }
  auto const y_size = static_cast<VkDeviceSize>(kExtent.width) * kExtent.height;
  auto const upload_size = y_size + y_size / kChromaSubsamplingDivisor;
  auto const &capabilities = device.capabilities.profiles[device.selection.profile_index].capabilities;
  auto const alignment = std::max<VkDeviceSize>(kOneResource, capabilities.minBitstreamBufferSizeAlignment);
  auto const requested = y_size * kBitstreamBytesPerPixel;
  auto const bitstream_size = ((requested + alignment - 1) / alignment) * alignment;
  VkVideoEncodeH264ProfileInfoKHR h264_profile{};
  h264_profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR;
  h264_profile.stdProfileIdc = device.selection.profile;
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
  Resources resources{ context };
  if (auto created = CreateResources(context, device, profiles, resources,
        upload_size, bitstream_size, graphics->second); !created) {
    return std::unexpected(created.error());
  }
  void *mapped = nullptr;
  auto result = context.disp.mapMemory(resources.upload_memory, 0, upload_size, 0, &mapped);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("map NV12 upload", result)); }
  std::memset(mapped, kNeutralLuma, static_cast<size_t>(y_size));
  std::memset(static_cast<std::byte *>(mapped) + y_size, kNeutralChroma,
    static_cast<size_t>(upload_size - y_size));
  context.disp.unmapMemory(resources.upload_memory);
  if (auto recorded = RecordUpload(context, resources, y_size); !recorded) {
    return std::unexpected(recorded.error());
  }
  if (auto recorded = RecordEncode(context, resources, session, device, bitstream_size); !recorded) {
    return std::unexpected(recorded.error());
  }
  if (auto recorded = RecordReadback(context, resources, bitstream_size); !recorded) {
    return std::unexpected(recorded.error());
  }
  if (auto submitted = Submit(context, graphics->first, resources.upload_cmd,
        VK_NULL_HANDLE, 0, resources.upload_done, VK_NULL_HANDLE); !submitted) {
    return std::unexpected(submitted.error());
  }
  if (auto submitted = Submit(context, device.queue, resources.video_cmd,
        resources.upload_done, VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
        resources.video_done, VK_NULL_HANDLE); !submitted) {
    return std::unexpected(submitted.error());
  }
  if (auto submitted = Submit(context, graphics->first, resources.readback_cmd,
        resources.video_done, VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_NULL_HANDLE, resources.readback_done); !submitted) {
    return std::unexpected(submitted.error());
  }
  result = context.disp.waitForFences(kOneResource, &resources.readback_done, VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("wait for encoded picture", result)); }
  struct Feedback
  {
    std::uint32_t offset;
    std::uint32_t size;
    VkQueryResultStatusKHR status;
  } feedback{};
  result = context.disp.getQueryPoolResults(resources.feedback, 0, kOneResource,
    sizeof(feedback), &feedback, sizeof(feedback), VK_QUERY_RESULT_WITH_STATUS_BIT_KHR);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("read encode feedback", result)); }
  if (feedback.status != VK_QUERY_RESULT_STATUS_COMPLETE_KHR || feedback.size == 0
      || feedback.offset > bitstream_size || feedback.size > bitstream_size - feedback.offset) {
    return std::unexpected(vkgsplat::MakeError(std::errc::io_error, "invalid encoded bitstream feedback"));
  }
  mapped = nullptr;
  result = context.disp.mapMemory(resources.readback_memory, 0, bitstream_size, 0, &mapped);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("map encoded picture", result)); }
  auto const *start = static_cast<std::byte const *>(mapped) + feedback.offset;
  std::vector<std::byte> picture(start, start + feedback.size);
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

auto Run(std::filesystem::path const &output) -> int
{
  std::error_code remove_error;
  std::filesystem::remove(output, remove_error);
  if (remove_error) {
    std::cerr << "failed to clear previous H.264 output: " << remove_error.message() << '\n';
    return EXIT_FAILURE;
  }
  auto platform = vkgsplat::OffscreenPlatform::create({ .width = kExtent.width, .height = kExtent.height });
  if (!platform) { return EXIT_FAILURE; }
  auto driver = vkgsplat::vulkan::VulkanDriver::create(*platform,
    { .enable_validation = VKGSPLAT_TEST_VIDEO_VALIDATION,
      .video_encode = vkgsplat::VideoEncodeSupport::kRequired });
  if (!driver) {
    if (driver.error().code() == std::make_error_code(std::errc::function_not_supported)
        && std::string_view{ driver.error().message() }.starts_with(kUnsupportedDeviceMessage)
        && !RequireVideoEncode()) {
      std::cout << "SKIP: " << driver.error().message() << '\n';
      return kSkipped;
    }
    std::cerr << driver.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto &context = (*driver)->context();
  if (!context.video_encode) { return EXIT_FAILURE; }
  auto device = *context.video_encode;
  auto selection = device.capabilities.select({
    .require_conversion = true,
    .require_references = false,
    .required_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
    .require_transfer_destination = true,
    .required_extent = kExtent,
  });
  if (!selection) {
    PrintSelectionDetails(device);
    std::cout << "1280x720 NV12 transfer destination unavailable: no conversion-capable selection\n";
    return RequireVideoEncode() ? EXIT_FAILURE : kSkipped;
  }
  if (selection->source_format.format != VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
      || selection->upload_strategy != vkgsplat::vulkan::video::UploadStrategy::kTransferDestination
      || kExtent.width > selection->max_extent.width || kExtent.height > selection->max_extent.height) {
    PrintSelectionDetails(device);
    std::cout << "1280x720 NV12 transfer destination unavailable: selected format "
              << static_cast<int>(selection->source_format.format)
              << ", upload strategy " << static_cast<int>(selection->upload_strategy)
              << ", maximum extent " << selection->max_extent.width << 'x'
              << selection->max_extent.height << '\n';
    return RequireVideoEncode() ? EXIT_FAILURE : kSkipped;
  }
  device.selection = *selection;
  auto session = VideoSession::Create(context, device, kExtent);
  if (!session) {
    if (session.error().code() == std::make_error_code(std::errc::function_not_supported)) {
      std::cout << "SKIP: " << session.error().message() << '\n';
      return kSkipped;
    }
    std::cerr << session.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto picture = EncodeSynthetic(context, device, **session);
  if (!picture) {
    if (picture.error().code() == std::make_error_code(std::errc::function_not_supported)) {
      std::cout << "SKIP: " << picture.error().message() << '\n';
      return kSkipped;
    }
    std::cerr << picture.error().message() << '\n';
    return EXIT_FAILURE;
  }
  auto writer = vkgsplat::H264Writer::Open(output.string(), (*session)->parameter_sets());
  if (!writer) {
    std::cerr << "open H.264 output: " << writer.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (auto written = writer->WriteFrame(*picture); !written) {
    std::cerr << "write H.264 picture: " << written.error().message() << '\n';
    return EXIT_FAILURE;
  }
  if (auto finished = writer->Finish(); !finished) {
    std::cerr << "finalize H.264 output: " << finished.error().message() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

}// namespace

auto main(int argc, char **argv) -> int
{
  constexpr int kExpectedArguments = 2;
  if (argc != kExpectedArguments) { return EXIT_FAILURE; }
  return Run(argv[1]);
}
