#pragma once

#include <cstdint>
#include <vector>
#include <vkexec/resource_use.hpp>

namespace vkgsplat {
struct RenderData;
}
namespace vkgsplat::gs {
// Non-owning identities. Supplying another frame slot needs no builder changes.
struct BufferBinding
{
  VkBuffer handle = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};
struct ImageBinding
{
  VkImage handle = VK_NULL_HANDLE;
  VkImageSubresourceRange range{};
  VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
struct PassResources
{
  std::vector<vkexec::buffer_use> buffers;
  std::vector<vkexec::image_use> images;
};
struct PassResourceBindings
{
  BufferBinding geometry;
  BufferBinding appearance;
  BufferBinding projected;
  BufferBinding projection_cameras;
  BufferBinding unsorted_keys;
  BufferBinding unsorted_values;
  BufferBinding instance_count;
  BufferBinding sorted_keys;
  BufferBinding sorted_values;
  BufferBinding sort_histogram;
  BufferBinding radix_dispatch;
  BufferBinding draw_indirect;
  BufferBinding tile_ranges;
  BufferBinding tiled_counts;
  BufferBinding tiled_offsets;
  BufferBinding tiled_total;
  BufferBinding tiled_overflow;
  BufferBinding tiled_radix_prefix;
  BufferBinding tiled_radix_group;
  BufferBinding tiled_debug_readback;
  std::vector<BufferBinding> scan_sums, scan_offsets;
  ImageBinding color_target;
};
struct RadixInvocation
{
  std::uint32_t iteration;
};
struct BatchInvocation
{
  std::uint32_t view_count;
  std::uint32_t splat_count;
};
struct ScanInvocation
{
  std::uint32_t level;
};
enum class PrefixPhase { Local, Groups, Add };
// All builders throw std::invalid_argument for missing identities or invalid ranges.
// They are audit/graph inputs only: none is invoked on the production frame path.
PassResourceBindings BindPassResources(RenderData const &data);
PassResources ProjectionResources(PassResourceBindings const &b);
PassResources CameraUploadResources(PassResourceBindings const &b, BatchInvocation invocation);
PassResources BatchedProjectionResources(PassResourceBindings const &b, BatchInvocation invocation);
PassResources CounterInitializationResources(PassResourceBindings const &b);
PassResources BinningResources(PassResourceBindings const &b);
PassResources SortPreparationResources(PassResourceBindings const &b);
PassResources RadixHistogramResources(PassResourceBindings const &b, RadixInvocation invocation);
PassResources RadixScatterResources(PassResourceBindings const &b, RadixInvocation invocation);
PassResources GraphicsRasterizationResources(PassResourceBindings const &b, bool retain_contents);
PassResources GraphicsRangeResources(PassResourceBindings const &b);
PassResources LegacyTiledRasterizationResources(PassResourceBindings const &b);
// Transfer operations remain separate from the shader invocation they precede.
PassResources FillResources(BufferBinding destination, VkDeviceSize offset, VkDeviceSize size);
PassResources CopyResources(BufferBinding source,
  BufferBinding destination,
  VkDeviceSize source_offset,
  VkDeviceSize destination_offset,
  VkDeviceSize size);
PassResources BlitResources(ImageBinding source, ImageBinding destination);
PassResources ImageReadbackResources(ImageBinding source, BufferBinding destination, VkDeviceSize size);
namespace tiled {
  PassResources CountResources(PassResourceBindings const &b);
  PassResources ScanResources(PassResourceBindings const &b, ScanInvocation invocation);
  PassResources ScanAddResources(PassResourceBindings const &b, ScanInvocation invocation);
  PassResources PrepareResources(PassResourceBindings const &b);
  PassResources EmitResources(PassResourceBindings const &b);
  PassResources RadixHistogramResources(PassResourceBindings const &b, RadixInvocation invocation);
  PassResources RadixPrefixResources(PassResourceBindings const &b, PrefixPhase phase);
  PassResources RadixScatterResources(PassResourceBindings const &b, RadixInvocation invocation);
  PassResources RangeResources(PassResourceBindings const &b);
  PassResources RasterizationResources(PassResourceBindings const &b);
  PassResources OverflowInitializationResources(PassResourceBindings const &b);
  PassResources RangeInitializationResources(PassResourceBindings const &b);
}// namespace tiled
}// namespace vkgsplat::gs
