#pragma once

#include <cstddef>
#include <vector>

#include "mesh.hpp"

#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <VkBootstrap.h>
#include <VkBootstrapDispatch.h>

#include <backend/vulkan/gpu_allocator.hpp>

namespace vkgsplat {

inline constexpr int k_max_frames_in_flight = 2;
inline constexpr size_t k_mesh_buffer_min_vertex_capacity = 16;

inline constexpr u32 k_grid_cols = 12;
inline constexpr u32 k_grid_rows = 12;
inline constexpr u32 k_triangle_count = k_grid_cols * k_grid_rows;
inline constexpr u32 k_sort_size = 256;
inline constexpr size_t k_sort_entry_size = sizeof(f32) + sizeof(u32);

struct Init
{
  GLFWwindow *window{};
  vkb::Instance instance{};
  vkb::InstanceDispatchTable inst_disp;
  VkSurfaceKHR surface{};
  vkb::Device device{};
  vkb::DispatchTable disp;
  vkb::Swapchain swapchain{};
  vulkan::GPUAllocator gpu_allocator;
  PFN_vkWriteResourceDescriptorsEXT write_resource_descriptors{};
  PFN_vkCmdBindResourceHeapEXT cmd_bind_resource_heap{};
};

struct RenderData
{
  VkQueue graphics_queue{};
  VkQueue present_queue{};

  std::vector<VkImage> swapchain_images;
  std::vector<VkImageView> swapchain_image_views;

  VkPipeline graphics_pipeline{};

  vulkan::Buffer position_buffer{};
  vulkan::Buffer color_buffer{};
  vulkan::Buffer sorted_indices_buffer{};
  vulkan::Buffer sort_entries_buffer{};
  vulkan::Buffer descriptor_heap_buffer{};
  VkDeviceSize descriptor_heap_size{};
  VkDeviceSize reserved_range_offset{};
  VkDeviceSize reserved_range_size{};
  size_t descriptor_stride{};

  VkPipeline sort_compute_pipeline{};

  Mesh mesh{};
  size_t mesh_buffer_vertex_capacity = 0;

  VkCommandPool command_pool{};
  std::vector<VkCommandBuffer> command_buffers;

  std::vector<VkSemaphore> available_semaphores;
  std::vector<VkSemaphore> finished_semaphore;
  std::vector<VkFence> in_flight_fences;
  std::vector<VkFence> image_in_flight;
  size_t current_frame = {};
};

}// namespace vkgsplat
