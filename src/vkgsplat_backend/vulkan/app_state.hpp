#ifndef VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP
#define VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "vulkan/command/buffer.hpp"
#include "vulkan/command/pool.hpp"
#include "vulkan/gpu_pass_timer.hpp"
#include "frame_context.hpp"
#include "present_pacer.hpp"
#include "gs/gaussian_splat.hpp"
#include "gs/push_constants.hpp"
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

#include <vkexec/gpu_buffer.hpp>
#include <vkexec/image.hpp>
#include <vkexec/image_view.hpp>
#include <vkexec/tensor.hpp>
#include <vkexec_extensions/descriptor_heap/algorithm.hpp>
#include <vkexec_extensions/descriptor_heap/buffer.hpp>
#include <vkexec_extensions/descriptor_heap/heap_graphics_pipeline.hpp>
#include <vkexec_extensions/timeline_semaphore/frame_ring.hpp>
#include <vkgsplat/lfd_config.hpp>


namespace vkgsplat {

// CPU/GPU pipelining depth: ring of frame slots.
// CPU ring (BoundedQueue + slot_ready) gates FrameSetup reuse after record/submit;
// frame_ring timeline + per-slot/image values gate GPU resource reuse.
constexpr size_t kFrameSlotCount = 2;

constexpr u32 kVertsPerSphere = 6;

struct ImGuiOverlayState;

struct RenderData
{
  u32 splat_count = 0;
  u32 sort_size = 0;
  VkQueue graphics_queue{};
  VkQueue present_queue{};

  std::optional<vkexec::heap_graphics_pipeline> graphics_pipeline;

  std::optional<vkexec::gpu_buffer> geometry_buffer;
  std::optional<vkexec::gpu_buffer> appearance_buffer;
  std::optional<vkexec::gpu_buffer> projected_buffer;
  std::optional<vkexec::gpu_buffer> unsorted_keys_buffer;
  std::optional<vkexec::gpu_buffer> unsorted_values_buffer;
  std::optional<vkexec::gpu_buffer> instance_count_buffer;
  std::optional<vkexec::gpu_buffer> sorted_keys_buffer;
  std::optional<vkexec::gpu_buffer> sorted_values_buffer;
  std::optional<vkexec::gpu_buffer> sort_histogram_buffer;
  std::optional<vkexec::gpu_buffer> radix_dispatch_buffer;
  std::optional<vkexec::gpu_buffer> draw_indirect_buffer;
  std::optional<vkexec::gpu_buffer> tile_ranges_buffer;
  std::optional<vkexec::image> color_image;
  std::optional<vkexec::image_view> color_image_view;
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
  // LFD quilt: mono is {1,1}. Color RT is an atlas of quilt_tile_extent cells.
  std::array<u32, 2> lfd_grid{ 1U, 1U };
  std::vector<u32> lfd_view_order;
  f64 view_cone_deg{ kDefaultViewConeDegrees };
  // Focal-plane distance (world units) for parallel-array view offsets.
  f64 lfd_focal_distance{ kDefaultLfdFocalDistance };
  VkExtent2D quilt_tile_extent{};
  bool lfd_emulate_active{ false };
  std::array<u32, 2> lfd_emulate_cell{ 0U, 0U };
  std::optional<vkexec::tensor<u32>> sorted_indices;
  std::optional<vkexec::tensor<gs::SortEntry>> sort_entries;
  std::optional<vkexec::descriptor_heap_buffer> descriptor_heap_buffer;
  VkDeviceSize descriptor_heap_size{};
  VkDeviceSize reserved_range_offset{};
  VkDeviceSize reserved_range_size{};
  size_t descriptor_stride{};
  size_t buffer_descriptor_size{};
  size_t image_descriptor_size{};

  std::optional<vkexec::heap_algorithm> project_algorithm;
  std::optional<vkexec::heap_algorithm> bin_algorithm;
  std::optional<vkexec::heap_algorithm> prepare_sort_algorithm;
  std::optional<vkexec::heap_algorithm> radix_histogram_algorithm;
  std::optional<vkexec::heap_algorithm> radix_scatter_algorithm;
  std::optional<vkexec::heap_algorithm> identify_ranges_algorithm;
  std::optional<vkexec::heap_algorithm> rasterize_algorithm;
  gs::ProjectPushConstants project_push{};
  gs::BinPushConstants bin_push{};
  gs::SortPushConstants sort_push{};
  gs::RasterPushConstants raster_push{};
  size_t present_image_index{};

  std::optional<vulkan::CommandPool> command_pool;
  std::vector<vulkan::CommandBuffer> command_buffers;

  // Binary WSI acquire/present semaphores + timeline gates for slot/image reuse.
  std::optional<vkexec::frame_ring> frame_ring;
  size_t current_slot = {};
  std::array<FrameContext, kFrameSlotCount> frames{};
  GpuPassTimer gpu_pass_timer;
  std::unique_ptr<ImGuiOverlayState> imgui;
  std::unique_ptr<PresentPacer> present_pacer;

  RenderData();
  ~RenderData();
  RenderData(RenderData &&) noexcept;
  auto operator=(RenderData &&) noexcept -> RenderData &;
  RenderData(RenderData const &) = delete;
  auto operator=(RenderData const &) -> RenderData & = delete;
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP
