#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <vector>

#include "types.hpp"
#include "sync_objects/fence.hpp"
#include "sync_objects/semaphore.hpp"
#include "compute/algorithm.hpp"
#include "compute/sequence.hpp"
#include "compute/sort_entry.hpp"
#include "compute/tensor.hpp"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

#include <backend/vulkan/command/buffer.hpp>
#include <backend/vulkan/command/pool.hpp>
#include <backend/vulkan/gpu_allocator.hpp>

namespace vkgsplat {

constexpr int k_max_frames_in_flight = 2;

constexpr u32 k_sphere_count = 64;
constexpr u32 k_sort_size = 64;
constexpr u32 k_verts_per_sphere = 6;
constexpr size_t k_sort_entry_size = sizeof(f32) + sizeof(u32);

struct RenderData
{
  VkQueue graphics_queue{};
  VkQueue present_queue{};

  VkPipeline graphics_pipeline{};

  vulkan::Buffer position_buffer{};
  vulkan::Buffer color_buffer{};
  compute::Tensor<u32> sorted_indices{};
  compute::Tensor<compute::SortEntry> sort_entries{};
  vulkan::Buffer descriptor_heap_buffer{};
  VkDeviceSize descriptor_heap_size{};
  VkDeviceSize reserved_range_offset{};
  VkDeviceSize reserved_range_size{};
  size_t descriptor_stride{};

  compute::Algorithm sphere_setup_algorithm{};
  compute::Sequence compute_sequence{};

  VkImage depth_image{};
  VmaAllocation depth_allocation{};
  VkImageView depth_image_view{};
  VkFormat depth_format{ VK_FORMAT_D32_SFLOAT };

  std::optional<vulkan::CommandPool> command_pool;
  std::vector<vulkan::CommandBuffer> command_buffers;

  std::vector<Semaphore> available_semaphores;
  std::vector<Semaphore> finished_semaphore;
  std::vector<Fence> in_flight_fences;
  std::vector<VkFence> image_in_flight;
  size_t current_frame = {};
};

}// namespace vkgsplat
