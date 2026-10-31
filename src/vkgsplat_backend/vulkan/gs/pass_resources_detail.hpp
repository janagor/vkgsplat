#pragma once
#include "pass_resources.hpp"
#include <stdexcept>
namespace vkgsplat::gs::resource_detail {
using A = vkexec::resource_access;
using U = vkexec::buffer_usage;
inline vkexec::buffer_use Buffer(BufferBinding b,
  A access,
  U usage = U::storage_compute,
  VkDeviceSize offset = 0,
  VkDeviceSize size = VK_WHOLE_SIZE)
{
  if (!b.handle || !b.size || b.size == VK_WHOLE_SIZE || offset >= b.size)
    throw std::invalid_argument("missing buffer or invalid extent/offset");
  if (size == VK_WHOLE_SIZE) size = b.size - offset;
  if (!size || size > b.size - offset) throw std::invalid_argument("buffer range exceeds allocation");
  return { b.handle, offset, size, usage, access };
}
// Reads of preserved contents must be safe even as the first graph use.
inline void RequireReadableImage(ImageBinding b)
{
  if (b.initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
    throw std::invalid_argument("preserved image contents require a known incoming layout");
}
inline vkexec::image_use Image(ImageBinding b, A access, vkexec::image_usage usage, VkImageLayout layout)
{
  if (!b.handle || b.range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT || !b.range.levelCount || !b.range.layerCount
      || b.range.levelCount == VK_REMAINING_MIP_LEVELS || b.range.layerCount == VK_REMAINING_ARRAY_LAYERS
      || b.range.baseMipLevel > UINT32_MAX - b.range.levelCount
      || b.range.baseArrayLayer > UINT32_MAX - b.range.layerCount)
    throw std::invalid_argument("missing color image or invalid finite subresource range");
  return { .image = b.handle,
    .range = b.range,
    .usage = usage,
    .access = access,
    .initial_layout = b.initial_layout,
    .layout = layout };
}
inline void Iteration(RadixInvocation i, std::uint32_t limit)
{
  if (i.iteration >= limit) throw std::invalid_argument("invalid radix iteration");
}
inline vkexec::buffer_use Indirect(PassResourceBindings const &b)
{ return Buffer(b.radix_dispatch, A::read, U::indirect, 0, sizeof(VkDispatchIndirectCommand)); }
}// namespace vkgsplat::gs::resource_detail
