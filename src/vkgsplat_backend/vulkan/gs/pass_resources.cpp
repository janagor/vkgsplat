#include "app_state.hpp"
#include "gs/push_constants.hpp"
#include "pass_resources_detail.hpp"
#include <limits>
namespace vkgsplat::gs {
using namespace resource_detail;
PassResourceBindings BindPassResources(RenderData const &data)
{
  PassResourceBindings b;
  auto bind = [](auto const &v) -> BufferBinding {
    return v ? BufferBinding{ v->handle(), v->size() } : BufferBinding{};
  };
  b.geometry = bind(data.geometry_buffer);
  b.appearance = bind(data.appearance_buffer);
  b.projected = bind(data.projected_buffer);
  b.projection_cameras = bind(data.projection_cameras_buffer);
  b.unsorted_keys = bind(data.unsorted_keys_buffer);
  b.unsorted_values = bind(data.unsorted_values_buffer);
  b.instance_count = bind(data.instance_count_buffer);
  b.sorted_keys = bind(data.sorted_keys_buffer);
  b.sorted_values = bind(data.sorted_values_buffer);
  b.sort_histogram = bind(data.sort_histogram_buffer);
  b.radix_dispatch = bind(data.radix_dispatch_buffer);
  b.draw_indirect = bind(data.draw_indirect_buffer);
  b.tile_ranges = bind(data.tile_ranges_buffer);
  b.tiled_counts = bind(data.tiled_counts_buffer);
  b.tiled_offsets = bind(data.tiled_offsets_buffer);
  b.tiled_total = bind(data.tiled_total_buffer);
  b.tiled_overflow = bind(data.tiled_overflow_buffer);
  b.tiled_radix_prefix = bind(data.tiled_radix_prefix_buffer);
  b.tiled_radix_group = bind(data.tiled_radix_group_buffer);
  b.tiled_debug_readback = bind(data.tiled_debug_readback_buffer);
  for (auto const &v : data.tiled_scan_sums) b.scan_sums.push_back({ v.handle(), v.size() });
  for (auto const &v : data.tiled_scan_offsets) b.scan_offsets.push_back({ v.handle(), v.size() });
  if (data.color_image) b.color_target = { data.color_image->handle(), { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
  return b;
}
PassResources ProjectionResources(PassResourceBindings const &b)
{ return { { Buffer(b.geometry, A::read), Buffer(b.appearance, A::read), Buffer(b.projected, A::write) }, {} }; }
namespace {
  VkDeviceSize CameraBytes(BatchInvocation i)
  {
    if (!i.view_count || !i.splat_count) throw std::invalid_argument("empty projection batch");
    auto bytes = VkDeviceSize{ i.view_count } * sizeof(ProjectPushConstants);
    if (bytes > 65536U) throw std::invalid_argument("camera upload exceeds cmdUpdateBuffer limit");
    return bytes;
  }
}// namespace
PassResources CameraUploadResources(PassResourceBindings const &b, BatchInvocation i)
{ return { { Buffer(b.projection_cameras, A::write, U::transfer_destination, 0, CameraBytes(i)) }, {} }; }
PassResources BatchedProjectionResources(PassResourceBindings const &b, BatchInvocation i)
{
  auto const cameras = CameraBytes(i);
  auto const elements = VkDeviceSize{ i.view_count } * i.splat_count;
  if (elements > std::numeric_limits<VkDeviceSize>::max() / sizeof(GaussianProjected))
    throw std::invalid_argument("projected batch size overflow");
  auto r = ProjectionResources(b);
  r.buffers.back() = Buffer(b.projected, A::write, U::storage_compute, 0, elements * sizeof(GaussianProjected));
  r.buffers.push_back(Buffer(b.projection_cameras, A::read, U::storage_compute, 0, cameras));
  return r;
}
PassResources FillResources(BufferBinding b, VkDeviceSize offset, VkDeviceSize size)
{
  if (offset % 4 || size % 4) throw std::invalid_argument("unaligned fill");
  return { { Buffer(b, A::write, U::transfer_destination, offset, size) }, {} };
}
PassResources
  CopyResources(BufferBinding source, BufferBinding destination, VkDeviceSize so, VkDeviceSize d, VkDeviceSize size)
{
  return { { Buffer(source, A::read, U::transfer_source, so, size),
             Buffer(destination, A::write, U::transfer_destination, d, size) },
    {} };
}
PassResources CounterInitializationResources(PassResourceBindings const &b)
{ return FillResources(b.instance_count, 0, 4); }
PassResources BinningResources(PassResourceBindings const &b)
{
  return { { Buffer(b.projected, A::read),
             Buffer(b.unsorted_keys, A::write),
             Buffer(b.unsorted_values, A::write),
             Buffer(b.instance_count, A::read_write, U::storage_compute, 0, 4) },
    {} };
}
PassResources SortPreparationResources(PassResourceBindings const &b)
{
  return { { Buffer(b.instance_count, A::read, U::storage_compute, 0, 4),
             Buffer(b.unsorted_keys, A::read),
             Buffer(b.unsorted_values, A::read),
             Buffer(b.sorted_keys, A::write),
             Buffer(b.sorted_values, A::write),
             Buffer(b.radix_dispatch, A::write, U::storage_compute, 0, sizeof(VkDispatchIndirectCommand)),
             Buffer(b.draw_indirect, A::write, U::storage_compute, 0, sizeof(VkDrawIndirectCommand)) },
    {} };
}
PassResources RadixHistogramResources(PassResourceBindings const &b, RadixInvocation i)
{
  Iteration(i, 4);
  return { { Buffer(i.iteration % 2 ? b.unsorted_keys : b.sorted_keys, A::read),
             Buffer(b.sort_histogram, A::write),
             Buffer(b.instance_count, A::read, U::storage_compute, 0, 4),
             Indirect(b) },
    {} };
}
PassResources RadixScatterResources(PassResourceBindings const &b, RadixInvocation i)
{
  Iteration(i, 4);
  return { { Buffer(i.iteration % 2 ? b.unsorted_keys : b.sorted_keys, A::read),
             Buffer(i.iteration % 2 ? b.unsorted_values : b.sorted_values, A::read),
             Buffer(i.iteration % 2 ? b.sorted_keys : b.unsorted_keys, A::write),
             Buffer(i.iteration % 2 ? b.sorted_values : b.unsorted_values, A::write),
             Buffer(b.sort_histogram, A::read),
             Buffer(b.instance_count, A::read, U::storage_compute, 0, 4),
             Indirect(b) },
    {} };
}
PassResources GraphicsRasterizationResources(PassResourceBindings const &b, bool retain)
{
  // Destination blending reads the attachment even after CLEAR.
  if (retain && b.color_target.initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)
    throw std::invalid_argument("retained attachment requires a known incoming layout");
  return {
    { Buffer(b.projected, A::read, U::storage_graphics),
      Buffer(b.sorted_values, A::read, U::storage_graphics),
      Buffer(b.draw_indirect, A::read, U::indirect, 0, sizeof(VkDrawIndirectCommand)) },
    { Image(
      b.color_target, A::read_write, vkexec::image_usage::color_attachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) }
  };
}
PassResources GraphicsRangeResources(PassResourceBindings const &b)
{
  return { { Buffer(b.sorted_keys, A::read),
             Buffer(b.instance_count, A::read, U::storage_compute, 0, 4),
             Buffer(b.tile_ranges, A::write) },
    {} };
}
PassResources LegacyTiledRasterizationResources(PassResourceBindings const &b)
{
  return { { Buffer(b.projected, A::read), Buffer(b.sorted_values, A::read), Buffer(b.tile_ranges, A::read) },
    { Image(b.color_target, A::write, vkexec::image_usage::storage_compute, VK_IMAGE_LAYOUT_GENERAL) } };
}
PassResources BlitResources(ImageBinding source, ImageBinding destination)
{
  RequireReadableImage(source);
  return { {},
    { Image(source, A::read, vkexec::image_usage::transfer_source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
      Image(destination, A::write, vkexec::image_usage::transfer_destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) } };
}
PassResources ImageReadbackResources(ImageBinding source, BufferBinding destination, VkDeviceSize size)
{
  RequireReadableImage(source);
  return { { Buffer(destination, A::write, U::transfer_destination, 0, size) },
    { Image(source, A::read, vkexec::image_usage::transfer_source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) } };
}
}// namespace vkgsplat::gs
