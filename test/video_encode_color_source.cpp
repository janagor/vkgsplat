// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
#include "video_encode_color_source.hpp"
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
#include <vector>

#include <VkBootstrap.h>
#include <vkgsplat_utility/error.hpp>
#include <vulkan/vulkan_core.h>

namespace vkgsplat::test {
namespace {

  constexpr std::uint32_t kOneResource = 1U;
  constexpr std::uint32_t kRgbaBytesPerPixel = 4U;
  constexpr std::uint32_t kHalf = 2U;
  constexpr std::uint32_t kQuadrantCount = 4U;
  constexpr std::uint8_t kOpaque = 255U;
  constexpr std::uint8_t kZero = 0U;
  constexpr std::array<std::array<std::uint8_t, kRgbaBytesPerPixel>, kQuadrantCount> kColors{
    std::array<std::uint8_t, kRgbaBytesPerPixel>{ kOpaque, kZero, kZero, kOpaque },
    std::array<std::uint8_t, kRgbaBytesPerPixel>{ kZero, kOpaque, kZero, kOpaque },
    std::array<std::uint8_t, kRgbaBytesPerPixel>{ kZero, kZero, kOpaque, kOpaque },
    std::array<std::uint8_t, kRgbaBytesPerPixel>{ kOpaque, kOpaque, kOpaque, kOpaque },
  };

  [[nodiscard]] auto Failure(char const *operation, VkResult result) -> Error
  {
    return MakeError(std::errc::io_error,
      std::string{ operation } + " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
  }

  [[nodiscard]] auto Allocate(vulkan::Context &context,
    VkMemoryRequirements const &requirements,
    VkMemoryPropertyFlags required) -> std::expected<VkDeviceMemory, Error>
  {
    VkPhysicalDeviceMemoryProperties properties{};
    context.inst_disp.getPhysicalDeviceMemoryProperties(context.device.physical_device, &properties);
    for (std::uint32_t index = 0U; index < properties.memoryTypeCount; ++index) {
      if ((requirements.memoryTypeBits & (1U << index)) == 0U
          || (std::span{ properties.memoryTypes }.subspan(index).front().propertyFlags & required) != required) {
        continue;
      }
      VkMemoryAllocateInfo const allocation{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = index,
      };
      VkDeviceMemory memory = VK_NULL_HANDLE;
      auto const result = context.disp.allocateMemory(&allocation, nullptr, &memory);
      if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate color test memory", result)); }
      return memory;
    }
    return std::unexpected(MakeError(std::errc::not_enough_memory, "no compatible color test memory type"));
  }

}// namespace

GpuColorSource::GpuColorSource(vulkan::Context &context, VkExtent2D extent) noexcept
  : context_{ context }, extent_{ extent }
{}

GpuColorSource::~GpuColorSource()
{
  if (submitted_ && graphics_queue_ != VK_NULL_HANDLE) {
    auto const queue_lock = context_.vkexec_context->lock_queue(graphics_queue_);
    context_.disp.queueWaitIdle(graphics_queue_);
  }
  if (ready_ != VK_NULL_HANDLE) { context_.disp.destroySemaphore(ready_, nullptr); }
  if (pool_ != VK_NULL_HANDLE) { context_.disp.destroyCommandPool(pool_, nullptr); }
  if (upload_ != VK_NULL_HANDLE) { context_.disp.destroyBuffer(upload_, nullptr); }
  if (image_ != VK_NULL_HANDLE) { context_.disp.destroyImage(image_, nullptr); }
  if (upload_memory_ != VK_NULL_HANDLE) { context_.disp.freeMemory(upload_memory_, nullptr); }
  if (image_memory_ != VK_NULL_HANDLE) { context_.disp.freeMemory(image_memory_, nullptr); }
}

auto GpuColorSource::Create(vulkan::Context &context, VkExtent2D extent)
  -> std::expected<std::unique_ptr<GpuColorSource>, Error>
{
  auto graphics = context.device.get_queue_and_index(vkb::QueueType::graphics);
  if (!graphics) {
    return std::unexpected(MakeError(std::errc::function_not_supported, "color test requires a graphics queue"));
  }
  auto source = std::unique_ptr<GpuColorSource>{ new GpuColorSource{ context, extent } };
  source->graphics_queue_ = graphics->first;
  VkImageCreateInfo const image{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0U,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .extent = { .width = extent.width, .height = extent.height, .depth = kOneResource },
    .mipLevels = kOneResource,
    .arrayLayers = kOneResource,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  auto result = context.disp.createImage(&image, nullptr, &source->image_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create color test image", result)); }
  VkMemoryRequirements requirements{};
  context.disp.getImageMemoryRequirements(source->image_, &requirements);
  auto memory = Allocate(context, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!memory) { return std::unexpected(memory.error()); }
  source->image_memory_ = *memory;
  result = context.disp.bindImageMemory(source->image_, source->image_memory_, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("bind color test image", result)); }
  auto const upload_size = static_cast<VkDeviceSize>(extent.width) * extent.height * kRgbaBytesPerPixel;
  VkBufferCreateInfo const buffer{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0U,
    .size = upload_size,
    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  result = context.disp.createBuffer(&buffer, nullptr, &source->upload_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create color test upload", result)); }
  context.disp.getBufferMemoryRequirements(source->upload_, &requirements);
  memory = Allocate(context, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!memory) { return std::unexpected(memory.error()); }
  source->upload_memory_ = *memory;
  result = context.disp.bindBufferMemory(source->upload_, source->upload_memory_, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("bind color test upload", result)); }
  VkCommandPoolCreateInfo const pool{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = graphics->second,
  };
  result = context.disp.createCommandPool(&pool, nullptr, &source->pool_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create color test command pool", result)); }
  VkCommandBufferAllocateInfo const allocate{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext = nullptr,
    .commandPool = source->pool_,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = kOneResource,
  };
  result = context.disp.allocateCommandBuffers(&allocate, &source->command_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("allocate color test command", result)); }
  VkSemaphoreTypeCreateInfo type{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
    .pNext = nullptr,
    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    .initialValue = 0U,
  };
  VkSemaphoreCreateInfo const semaphore{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = &type,
  };
  result = context.disp.createSemaphore(&semaphore, nullptr, &source->ready_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create color test timeline", result)); }
  return source;
}

auto GpuColorSource::Prepare() -> std::expected<void, Error>
{
  if (submitted_) {
    return std::unexpected(MakeError(std::errc::operation_in_progress, "color test source already submitted"));
  }
  auto const upload_size = static_cast<VkDeviceSize>(extent_.width) * extent_.height * kRgbaBytesPerPixel;
  void *mapped = nullptr;
  auto result = context_.disp.mapMemory(upload_memory_, 0U, upload_size, 0U, &mapped);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("map color test upload", result)); }
  auto *pixels = static_cast<std::uint8_t *>(mapped);
  auto const pixel_bytes = std::span{ pixels, static_cast<std::size_t>(upload_size) };
  for (std::uint32_t row = 0U; row < extent_.height; ++row) {
    for (std::uint32_t column = 0U; column < extent_.width; ++column) {
      auto const quadrant =
        (row >= extent_.height / kHalf ? kHalf : 0U) + (column >= extent_.width / kHalf ? kOneResource : 0U);
      auto const offset = ((static_cast<std::size_t>(row) * extent_.width) + column) * kRgbaBytesPerPixel;
      for (std::size_t channel = 0U; channel < kRgbaBytesPerPixel; ++channel) {
        pixel_bytes.subspan(offset + channel).front() = kColors.at(quadrant).at(channel);
      }
    }
  }
  context_.disp.unmapMemory(upload_memory_);
  VkCommandBufferBeginInfo const begin{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = nullptr,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  result = context_.disp.beginCommandBuffer(command_, &begin);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("begin color test command", result)); }
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
    .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0U,
      .levelCount = kOneResource,
      .baseArrayLayer = 0U,
      .layerCount = kOneResource },
  };
  VkDependencyInfo const dependency{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .pNext = nullptr,
    .imageMemoryBarrierCount = kOneResource,
    .pImageMemoryBarriers = &barrier,
  };
  context_.disp.cmdPipelineBarrier2(command_, &dependency);
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = kOneResource;
  region.imageExtent = { .width = extent_.width, .height = extent_.height, .depth = kOneResource };
  context_.disp.cmdCopyBufferToImage(
    command_, upload_, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kOneResource, &region);
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
  barrier.dstAccessMask = 0U;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  context_.disp.cmdPipelineBarrier2(command_, &dependency);
  result = context_.disp.endCommandBuffer(command_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("end color test command", result)); }
  VkCommandBufferSubmitInfo const command{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    .pNext = nullptr,
    .commandBuffer = command_,
  };
  VkSemaphoreSubmitInfo const signal{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .pNext = nullptr,
    .semaphore = ready_,
    .value = kReadyValue,
    .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
  };
  VkSubmitInfo2 const submit{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .pNext = nullptr,
    .commandBufferInfoCount = kOneResource,
    .pCommandBufferInfos = &command,
    .signalSemaphoreInfoCount = kOneResource,
    .pSignalSemaphoreInfos = &signal,
  };
  auto const queue_lock = context_.vkexec_context->lock_queue(graphics_queue_);
  result = context_.disp.queueSubmit2(graphics_queue_, kOneResource, &submit, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("submit color test upload", result)); }
  submitted_ = true;
  return {};
}

auto GpuColorSource::ReadNv12Buffer(VkBuffer source, VkDeviceSize size) -> std::expected<std::vector<std::byte>, Error>
{
  if (!submitted_ || source == VK_NULL_HANDLE || size == 0U) {
    return std::unexpected(MakeError(std::errc::invalid_argument, "invalid converted NV12 readback"));
  }
  struct Readback
  {
    vulkan::Context *context;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    explicit Readback(vulkan::Context &owner) noexcept : context{ &owner } {}
    Readback(Readback const &) = delete;
    auto operator=(Readback const &) -> Readback & = delete;
    Readback(Readback &&) = delete;
    auto operator=(Readback &&) -> Readback & = delete;

    ~Readback()
    {
      if (buffer != VK_NULL_HANDLE) { context->disp.destroyBuffer(buffer, nullptr); }
      if (memory != VK_NULL_HANDLE) { context->disp.freeMemory(memory, nullptr); }
    }
  } readback{ context_ };
  VkBufferCreateInfo const buffer{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0U,
    .size = size,
    .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  auto result = context_.disp.createBuffer(&buffer, nullptr, &readback.buffer);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("create NV12 test readback", result)); }
  VkMemoryRequirements requirements{};
  context_.disp.getBufferMemoryRequirements(readback.buffer, &requirements);
  auto memory =
    Allocate(context_, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!memory) { return std::unexpected(memory.error()); }
  readback.memory = *memory;
  result = context_.disp.bindBufferMemory(readback.buffer, readback.memory, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("bind NV12 test readback", result)); }
  result = context_.disp.resetCommandBuffer(command_, 0U);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("reset NV12 test command", result)); }
  VkCommandBufferBeginInfo const begin{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .pNext = nullptr,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  result = context_.disp.beginCommandBuffer(command_, &begin);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("begin NV12 test command", result)); }
  VkBufferCopy const region{ .srcOffset = 0U, .dstOffset = 0U, .size = size };
  context_.disp.cmdCopyBuffer(command_, source, readback.buffer, kOneResource, &region);
  VkBufferMemoryBarrier2 const barrier{
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
    .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
    .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = readback.buffer,
    .offset = 0U,
    .size = size,
  };
  VkDependencyInfo const dependency{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .pNext = nullptr,
    .bufferMemoryBarrierCount = kOneResource,
    .pBufferMemoryBarriers = &barrier,
  };
  context_.disp.cmdPipelineBarrier2(command_, &dependency);
  result = context_.disp.endCommandBuffer(command_);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("end NV12 test command", result)); }
  VkCommandBufferSubmitInfo const command{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    .pNext = nullptr,
    .commandBuffer = command_,
  };
  VkSubmitInfo2 const submit{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .pNext = nullptr,
    .commandBufferInfoCount = kOneResource,
    .pCommandBufferInfos = &command,
  };
  {
    auto const queue_lock = context_.vkexec_context->lock_queue(graphics_queue_);
    result = context_.disp.queueSubmit2(graphics_queue_, kOneResource, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) { return std::unexpected(Failure("submit NV12 test readback", result)); }
    result = context_.disp.queueWaitIdle(graphics_queue_);
  }
  if (result != VK_SUCCESS) { return std::unexpected(Failure("wait for NV12 test readback", result)); }
  void *mapped = nullptr;
  result = context_.disp.mapMemory(readback.memory, 0U, size, 0U, &mapped);
  if (result != VK_SUCCESS) { return std::unexpected(Failure("map NV12 test readback", result)); }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  std::memcpy(bytes.data(), mapped, bytes.size());
  context_.disp.unmapMemory(readback.memory);
  return bytes;
}

}// namespace vkgsplat::test
// NOLINTEND(readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
