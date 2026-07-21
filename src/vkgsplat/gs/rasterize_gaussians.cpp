#include "gs/rasterize_gaussians.hpp"

#include "app_state.hpp"
#include "backend/vulkan/descriptor/descriptor_heap.hpp"
#include "backend/vulkan/initializers.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/push_constants.hpp"
#include "vulkan_context.hpp"

#include <vkgsplat/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::gs {

namespace {

  void destroy_color_target(Init &init, RenderData &data)
  {
    if (data.color_image != VK_NULL_HANDLE || data.color_allocation != VK_NULL_HANDLE) {
      vmaDestroyImage(init.gpu_allocator.vma_allocator(), data.color_image, data.color_allocation);
      data.color_image = VK_NULL_HANDLE;
      data.color_allocation = VK_NULL_HANDLE;
    }
    init.gpu_allocator.destroy_buffer(data.color_buffer);
    data.color_buffer = {};
    data.color_width = 0;
    data.color_height = 0;
  }

  [[nodiscard]] auto create_color_target(Init &init, RenderData &data) -> bool
  {
    destroy_color_target(init, data);

    data.color_width = init.swapchain->extent().width;
    data.color_height = init.swapchain->extent().height;
    if (data.color_width == 0 || data.color_height == 0) {
      std::println("Rasterize requires a non-zero swapchain extent!");
      return false;
    }

    auto const pixel_count = static_cast<VkDeviceSize>(data.color_width) * data.color_height;
    auto const buffer_size = pixel_count * 4U * sizeof(f32);
    auto color_buffer = init.gpu_allocator.create_device_storage_buffer(buffer_size);
    if (!color_buffer) {
      std::println("Failed to create color storage buffer!");
      destroy_color_target(init, data);
      return false;
    }
    data.color_buffer = *color_buffer;

    auto image_info = initializers::ImageCreateInfo();
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = data.color_format;
    image_info.extent = { .width = data.color_width, .height = data.color_height, .depth = 1 };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(init.gpu_allocator.vma_allocator(),
          &image_info,
          &alloc_info,
          &data.color_image,
          &data.color_allocation,
          nullptr)
        != VK_SUCCESS) {
      std::println("Failed to create color transfer image!");
      destroy_color_target(init, data);
      return false;
    }

    return true;
  }

  void
    push_raster_constants(Init const &init, RasterPushConstants const &push_constants, VkCommandBuffer command_buffer)
  {
    VkPushDataInfoEXT const push_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT,
      .pNext = nullptr,
      .offset = 0,
      .data = { .address = &push_constants, .size = sizeof(RasterPushConstants) },
    };
    init.cmd_push_data(command_buffer, &push_info);
  }

}// namespace

auto init_rasterize_gaussians(Init &init, RenderData &data) -> bool
{
  if (!create_color_target(init, data)) { return false; }

  std::string const shader_path = std::string(SHADER_DIRECTORY) + "/rasterize_gaussians.comp.spv";
  if (!data.rasterize_algorithm.init(init, shader_path)) {
    destroy_color_target(init, data);
    return false;
  }

  return refresh_descriptor_heap(init, data);
}

auto recreate_rasterize_color_target(Init &init, RenderData &data) -> bool
{
  if (!create_color_target(init, data)) { return false; }
  return refresh_descriptor_heap(init, data);
}

void dispatch_rasterize_gaussians(Init const &init,
  RenderData const &data,
  RasterPushConstants const &push_constants,
  VkCommandBuffer command_buffer,
  size_t image_index)
{
  u32 const tiles_x = (data.color_width + k_tile_size - 1U) / k_tile_size;
  u32 const tiles_y = (data.color_height + k_tile_size - 1U) / k_tile_size;

  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, data.rasterize_algorithm.pipeline());
  push_raster_constants(init, push_constants, command_buffer);
  init.disp.cmdDispatch(command_buffer, tiles_x, tiles_y, 1U);

  VkMemoryBarrier const compute_done = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
  };
  VkBufferMemoryBarrier const color_buffer_barrier = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .buffer = data.color_buffer.handle,
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,
    1,
    &compute_done,
    1,
    &color_buffer_barrier,
    0,
    nullptr);

  VkImageSubresourceRange const color_range = {
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  auto color_to_dst = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, data.color_image, color_range);
  color_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  auto swap_to_dst = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    init.swapchain->images().at(image_index),
    color_range);
  swap_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  std::array<VkImageMemoryBarrier, 2> prep_barriers = { color_to_dst, swap_to_dst };
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    static_cast<uint32_t>(prep_barriers.size()),
    prep_barriers.data());

  VkBufferImageCopy const copy_region = {
    .bufferOffset = 0,
    .bufferRowLength = 0,
    .bufferImageHeight = 0,
    .imageSubresource =
      {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    .imageOffset = { .x = 0, .y = 0, .z = 0 },
    .imageExtent = { .width = data.color_width, .height = data.color_height, .depth = 1 },
  };
  init.disp.cmdCopyBufferToImage(
    command_buffer, data.color_buffer.handle, data.color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

  auto color_to_src = initializers::ImageMemoryBarrier(
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.color_image, color_range);
  color_to_src.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  color_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &color_to_src);

  VkImageBlit const blit = {
    .srcSubresource =
      {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    .srcOffsets =
      {
        { .x = 0, .y = 0, .z = 0 },
        { .x = static_cast<int32_t>(data.color_width),
          .y = static_cast<int32_t>(data.color_height),
          .z = 1 },
      },
    .dstSubresource =
      {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    .dstOffsets =
      {
        { .x = 0, .y = 0, .z = 0 },
        { .x = static_cast<int32_t>(init.swapchain->extent().width),
          .y = static_cast<int32_t>(init.swapchain->extent().height),
          .z = 1 },
      },
  };
  init.disp.cmdBlitImage(command_buffer,
    data.color_image,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    init.swapchain->images().at(image_index),
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1,
    &blit,
    VK_FILTER_NEAREST);

  auto present_barrier = initializers::ImageMemoryBarrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    init.swapchain->images().at(image_index),
    color_range);
  present_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  init.disp.cmdPipelineBarrier(command_buffer,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    0,
    0,
    nullptr,
    0,
    nullptr,
    1,
    &present_barrier);
}

void destroy_rasterize_gaussians(Init &init, RenderData &data)
{
  data.rasterize_algorithm.destroy(init);
  destroy_color_target(init, data);
}

}// namespace vkgsplat::gs
