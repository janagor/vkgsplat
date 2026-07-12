#include "depth_buffer.hpp"

#include <print>

#include "app_state.hpp"
#include "initializers.hpp"
#include "vulkan_context.hpp"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

namespace vkgsplat {

void destroy_depth_buffer(Init &init, RenderData &data)
{
  if (data.depth_image_view != VK_NULL_HANDLE) {
    init.disp.destroyImageView(data.depth_image_view, nullptr);
    data.depth_image_view = VK_NULL_HANDLE;
  }

  if (data.depth_image != VK_NULL_HANDLE || data.depth_allocation != VK_NULL_HANDLE) {
    vmaDestroyImage(init.gpu_allocator.vma_allocator(), data.depth_image, data.depth_allocation);
    data.depth_image = VK_NULL_HANDLE;
    data.depth_allocation = VK_NULL_HANDLE;
  }
}

auto create_depth_buffer(Init &init, RenderData &data) -> bool
{
  destroy_depth_buffer(init, data);

  auto image_info = initializers::ImageCreateInfo();
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = data.depth_format;
  image_info.extent = { .width = init.swapchain->extent().width,
    .height = init.swapchain->extent().height,
    .depth = 1 };
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo alloc_info = {};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

  if (vmaCreateImage(init.gpu_allocator.vma_allocator(), &image_info, &alloc_info, &data.depth_image, &data.depth_allocation, nullptr)
      != VK_SUCCESS) {
    std::println("Failed to create depth image!");
    return false;
  }

  VkImageSubresourceRange const subresource_range = {
    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };

  auto const view_info =
    initializers::ImageViewCreateInfo(data.depth_image, VK_IMAGE_VIEW_TYPE_2D, data.depth_format, subresource_range);

  if (init.disp.createImageView(&view_info, nullptr, &data.depth_image_view) != VK_SUCCESS) {
    std::println("Failed to create depth image view!");
    destroy_depth_buffer(init, data);
    return false;
  }

  return true;
}

}// namespace vkgsplat
