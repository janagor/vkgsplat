#ifndef VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP
#define VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "frame_context.hpp"
#include "gpu_buffers.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/push_constants.hpp"
#include "present_pacer.hpp"
#include "vulkan/command/buffer.hpp"
#include "vulkan/command/pool.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

#include <vkexec/image_view.hpp>
#include <vkexec/tensor.hpp>
#include <vkexec_extensions/descriptor_heap/algorithm.hpp>
#include <vkexec_extensions/descriptor_heap/buffer.hpp>
#include <vkexec_extensions/descriptor_heap/heap_graphics_pipeline.hpp>
#include <vkexec_extensions/timeline_semaphore/frame_ring.hpp>
#include <vkexec_vma/image.hpp>
#include <vkgsplat/lfd_config.hpp>
#include <vkgsplat/renderer.hpp>


namespace vkgsplat {

using GpuBuffer = vulkan::GpuBuffer;
using DescriptorHeapBuffer = vulkan::DescriptorHeapBuffer;
using GpuImage = vkexec::vma::image;

template<typename T> using Tensor = vkexec::tensor<T, GpuBuffer>;

// CPU/GPU pipelining depth: ring of frame slots.
// RenderThread tracks CPU slot ownership; frame_ring gates GPU resource reuse.
constexpr size_t kFrameSlotCount = 2;

constexpr u32 kVertsPerSphere = 6;
constexpr u32 kDefaultProjectionBatchSize = 4U;
constexpr u32 kMaxProjectionBatchSize = 8U;
enum class TiledDebugSection : u8 {
  kProjected,
  kCounts,
  kOffsets,
  kEmittedKeys,
  kEmittedIndices,
  kSortedKeys,
  kSortedIndices,
  kRanges,
  kCount,
};
constexpr size_t kTiledDebugSectionCount = static_cast<size_t>(TiledDebugSection::kCount);

struct ImGuiOverlayState;

struct RenderData
{
  VkQueue graphics_queue{};
  VkQueue present_queue{};
  u64 tiled_intersection_budget_bytes{ kDefaultTiledIntersectionBudgetBytes };
  f64 view_cone_deg{ kDefaultViewConeDegrees };
  // Focal-plane distance (world units) for parallel-array view offsets.
  f64 lfd_focal_distance{ kDefaultLfdFocalDistance };
  VkDeviceSize descriptor_heap_size{};
  VkDeviceSize descriptor_heap_alignment{};
  VkDeviceSize reserved_range_offset{};
  VkDeviceSize reserved_range_size{};
  VkDeviceSize sampler_heap_size{};
  VkDeviceSize sampler_heap_alignment{};
  VkDeviceSize sampler_reserved_range_offset{};
  VkDeviceSize sampler_reserved_range_size{};
  size_t descriptor_stride{};
  size_t buffer_descriptor_size{};
  size_t image_descriptor_size{};
  size_t sampler_descriptor_size{};
  size_t present_image_index{};
  VkSemaphore offscreen_timeline{ VK_NULL_HANDLE };
  u64 offscreen_signal_value{ 0U };
  size_t current_slot = {};
  std::unique_ptr<ImGuiOverlayState> imgui;
  std::unique_ptr<PresentPacer> present_pacer;
  std::optional<vkexec::owned::descriptor_graphics_pipeline> graphics_pipeline;
  std::vector<GpuBuffer> tiled_scan_sums;
  std::vector<GpuBuffer> tiled_scan_offsets;
  std::optional<vkexec::owned::image_view> color_image_view;
  std::vector<u32> lfd_view_order;
  std::optional<vkexec::algorithm> project_algorithm;
  std::optional<vkexec::algorithm> project_batch_algorithm;
  std::optional<vkexec::algorithm> bin_algorithm;
  std::optional<vkexec::algorithm> prepare_sort_algorithm;
  std::optional<vkexec::algorithm> radix_histogram_algorithm;
  std::optional<vkexec::algorithm> radix_scatter_algorithm;
  std::optional<vkexec::algorithm> identify_ranges_algorithm;
  std::optional<vkexec::algorithm> rasterize_algorithm;
  std::optional<vkexec::algorithm> tiled_count_algorithm;
  std::optional<vkexec::algorithm> tiled_scan_algorithm;
  std::optional<vkexec::algorithm> tiled_emit_algorithm;
  std::optional<vkexec::algorithm> tiled_prepare_algorithm;
  std::optional<vkexec::algorithm> tiled_histogram_algorithm;
  std::optional<vkexec::algorithm> tiled_prefix_algorithm;
  std::optional<vkexec::algorithm> tiled_scatter_algorithm;
  std::optional<vkexec::algorithm> tiled_ranges_algorithm;
  std::optional<vkexec::algorithm> tiled_raster_algorithm;
  gs::BinPushConstants bin_push{};
  std::optional<vulkan::CommandPool> command_pool;
  std::vector<vulkan::CommandBuffer> command_buffers;
  std::vector<GpuBuffer> in_flight_readbacks;
  std::optional<Tensor<u32>> sorted_indices;
  std::optional<Tensor<gs::SortEntry>> sort_entries;
  gs::SortPushConstants sort_push{};
  std::optional<GpuBuffer> geometry_buffer;
  std::optional<GpuBuffer> appearance_buffer;
  std::optional<GpuBuffer> projected_buffer;
  std::optional<GpuBuffer> projection_cameras_buffer;
  std::optional<GpuBuffer> unsorted_keys_buffer;
  std::optional<GpuBuffer> unsorted_values_buffer;
  std::optional<GpuBuffer> instance_count_buffer;
  std::optional<GpuBuffer> sorted_keys_buffer;
  std::optional<GpuBuffer> sorted_values_buffer;
  std::optional<GpuBuffer> sort_histogram_buffer;
  std::optional<GpuBuffer> radix_dispatch_buffer;
  std::optional<GpuBuffer> draw_indirect_buffer;
  std::optional<GpuBuffer> tile_ranges_buffer;
  std::optional<GpuBuffer> tiled_counts_buffer;
  std::optional<GpuBuffer> tiled_offsets_buffer;
  std::optional<GpuBuffer> tiled_total_buffer;
  std::optional<GpuBuffer> tiled_overflow_buffer;
  std::optional<GpuBuffer> tiled_radix_prefix_buffer;
  std::optional<GpuBuffer> tiled_radix_group_buffer;
  std::optional<GpuBuffer> tiled_debug_readback_buffer;
  std::array<VkDeviceSize, kTiledDebugSectionCount> tiled_debug_offsets{};
  std::array<VkDeviceSize, kTiledDebugSectionCount> tiled_debug_sizes{};
  std::optional<DescriptorHeapBuffer> descriptor_heap_buffer;
  std::optional<DescriptorHeapBuffer> sampler_heap_buffer;
  std::optional<GpuImage> color_image;
  // Binary WSI acquire/present semaphores + timeline gates for slot/image reuse.
  std::optional<vkexec::owned::frame_ring> frame_ring;
  GpuPassTimer gpu_pass_timer;
  std::array<FrameContext, kFrameSlotCount> frames{};
  u32 splat_count = 0;
  u32 sort_size = 0;
  u32 lfd_projection_batch_size{ kDefaultProjectionBatchSize };
  u32 tiled_radix_prefix_groups{};
  u32 tiled_intersection_capacity{};
  u32 tiled_tiles_x{};
  u32 tiled_tiles_y{};
  u32 tiled_radix_workgroups{};
  // 3DGS is trained for gamma-space blending (typical WebGL UNORM8). A float RT
  // blends the same gamma-coded SH colors with linear-like precision and shifts hue.
  VkFormat color_format{ VK_FORMAT_R8G8B8A8_UNORM };
  u32 max_bin_instances = 0;
  u32 gaussian_sort_size = 0;
  u32 radix_num_workgroups = 0;
  u32 radix_blocks_per_workgroup = gs::kRadixBlocksPerWorkgroup;
  u32 tile_count = 0;
  u32 color_width = 0;
  u32 color_height = 0;
  VkExtent2D output_extent{};
  // LFD quilt: mono is {1,1}. Color RT is an atlas of quilt_tile_extent cells.
  std::array<u32, 2> lfd_grid{ 1U, 1U };
  std::array<u32, 2> quilt_size{ 0U, 0U };
  VkExtent2D quilt_tile_extent{};
  std::array<u32, 2> lfd_emulate_cell{ 0U, 0U };
  gs::RasterPushConstants raster_push{};
  gs::ProjectPushConstants project_push{};
  RasterizationMode rasterization_mode{ RasterizationMode::kGraphicsPipeline };
  bool tiled_status_pending{};
  bool enable_tiled_debug_readback{};
  bool lfd_emulate_active{ false };

  RenderData();
  ~RenderData();
  RenderData(RenderData &&) noexcept;
  auto operator=(RenderData &&) noexcept -> RenderData &;
  RenderData(RenderData const &) = delete;
  auto operator=(RenderData const &) -> RenderData & = delete;
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP
