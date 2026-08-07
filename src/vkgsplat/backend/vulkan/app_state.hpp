#ifndef VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP
#define VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "backend/vulkan/command/buffer.hpp"
#include "backend/vulkan/command/pool.hpp"
#include "backend/vulkan/gpu_allocator.hpp"
#include "backend/vulkan/gpu_pass_timer.hpp"
#include "backend/vulkan/sync_objects/fence.hpp"
#include "backend/vulkan/sync_objects/semaphore.hpp"
#include "frame_context.hpp"
#include "compute/algorithm.hpp"
#include "compute/sequence.hpp"
#include "compute/sort_entry.hpp"
#include "compute/tensor.hpp"
#include "gs/push_constants.hpp"
#include <vkgsplat_utility/types.hpp>

#include <array>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>


namespace vkgsplat {

constexpr int kMaxFramesInFlight = 2;

constexpr u32 kVertsPerSphere = 6;
constexpr size_t kSortEntrySize = sizeof(f32) + sizeof(u32);

struct ImGuiOverlayState;

struct RenderData
{
  u32 splat_count = 0;
  u32 sort_size = 0;
  VkQueue graphics_queue{};
  VkQueue present_queue{};

  VkPipeline graphics_pipeline{};

  vulkan::Buffer geometry_buffer{};
  vulkan::Buffer appearance_buffer{};
  vulkan::Buffer projected_buffer{};
  vulkan::Buffer unsorted_keys_buffer{};
  vulkan::Buffer unsorted_values_buffer{};
  vulkan::Buffer instance_count_buffer{};
  vulkan::Buffer sorted_keys_buffer{};
  vulkan::Buffer sorted_values_buffer{};
  vulkan::Buffer sort_histogram_buffer{};
  vulkan::Buffer radix_dispatch_buffer{};
  vulkan::Buffer draw_indirect_buffer{};
  vulkan::Buffer tile_ranges_buffer{};
  VkImage color_image{};
  VmaAllocation color_allocation{};
  VkImageView color_image_view{};
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
  compute::Tensor<u32> sorted_indices;
  compute::Tensor<compute::SortEntry> sort_entries;
  vulkan::Buffer descriptor_heap_buffer{};
  VkDeviceSize descriptor_heap_size{};
  VkDeviceSize reserved_range_offset{};
  VkDeviceSize reserved_range_size{};
  size_t descriptor_stride{};
  size_t buffer_descriptor_size{};
  size_t image_descriptor_size{};

  compute::Algorithm project_algorithm;
  compute::Algorithm bin_algorithm;
  compute::Algorithm prepare_sort_algorithm;
  compute::Algorithm radix_histogram_algorithm;
  compute::Algorithm radix_scatter_algorithm;
  compute::Algorithm identify_ranges_algorithm;
  compute::Algorithm rasterize_algorithm;
  compute::Sequence gs_sequence;
  gs::ProjectPushConstants project_push{};
  gs::BinPushConstants bin_push{};
  gs::SortPushConstants sort_push{};
  gs::RasterPushConstants raster_push{};
  size_t present_image_index{};

  std::optional<vulkan::CommandPool> command_pool;
  std::vector<vulkan::CommandBuffer> command_buffers;

  std::vector<Semaphore> available_semaphores;
  std::vector<Semaphore> finished_semaphore;
  std::vector<Fence> in_flight_fences;
  std::vector<VkFence> image_in_flight;
  size_t current_frame = {};
  std::array<FrameContext, kMaxFramesInFlight> frames{};
  GpuPassTimer gpu_pass_timer;
  std::unique_ptr<ImGuiOverlayState> imgui;

  RenderData();
  ~RenderData();
  RenderData(RenderData &&) noexcept;
  auto operator=(RenderData &&) noexcept -> RenderData &;
  RenderData(RenderData const &) = delete;
  auto operator=(RenderData const &) -> RenderData & = delete;
};

}// namespace vkgsplat

#endif// VKGSPLAT_BACKEND_VULKAN_APP_STATE_HPP
