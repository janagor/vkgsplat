#include "compute_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "descriptor_heap.hpp"
#include "initializers.hpp"
#include "shader.hpp"

#include <vulkan/vulkan_core.h>

#include <VkBootstrap.h>

#include "vkgsplat/example_config.h"

namespace vkgsplat::compute {

void run_compute_test(Init &init)
{
  std::println("--- Rozpoczynam test Compute Shadera (Dodawanie 2 tablic) ---");

  auto compute_queue_res = init.device.get_queue(vkb::QueueType::compute);
  if (!compute_queue_res) {
    std::println("Brak kolejki obliczeniowej!");
    return;
  }
  VkQueue compute_queue = compute_queue_res.value();

  uint32_t const element_count = 1024;
  VkDeviceSize const buffer_size = element_count * sizeof(float);

  auto bufferA = init.gpu_allocator.create_storage_buffer(buffer_size);
  auto bufferB = init.gpu_allocator.create_storage_buffer(buffer_size);
  auto bufferResult = init.gpu_allocator.create_storage_buffer(buffer_size);
  if (!bufferA || !bufferB || !bufferResult) {
    std::println("Nie udało się utworzyć buforów!");
    return;
  }

  auto const input = std::views::iota(0U, element_count) | std::ranges::to<std::vector<float>>();

  if (!init.gpu_allocator.write_buffer(*bufferA, std::span{ input })) {
    std::println("Nie udało się zapisać danych do bufora A!");
    return;
  }

  if (!init.gpu_allocator.write_buffer(*bufferB, std::span{ input })) {
    std::println("Nie udało się zapisać danych do bufora B!");
    return;
  }

  VkPhysicalDeviceDescriptorHeapPropertiesEXT heap_props{};
  heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;

  VkPhysicalDeviceProperties2 props2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &heap_props,
    .properties = init.device.physical_device.properties,
  };
  init.inst_disp.getPhysicalDeviceProperties2(init.device.physical_device, &props2);

  auto const descriptor_size = static_cast<size_t>(heap_props.bufferDescriptorSize);
  auto const descriptor_stride =
    static_cast<size_t>(align_up(heap_props.bufferDescriptorSize, heap_props.bufferDescriptorAlignment));
  VkDeviceSize const descriptor_region_size = descriptor_stride * 3;
  VkDeviceSize const reserved_range_offset = align_up(descriptor_region_size, heap_props.resourceHeapAlignment);
  VkDeviceSize const reserved_range_size = heap_props.minResourceHeapReservedRange;
  VkDeviceSize const heap_size = reserved_range_offset + reserved_range_size;

  auto bufferHeap = init.gpu_allocator.create_heap_buffer(heap_size);
  if (!bufferHeap) {
    std::println("Failed to create a heap buffer!");
    return;
  }

  std::vector<std::byte> descriptor_data(descriptor_stride * 3);
  std::array<VkDeviceAddressRangeEXT, 3> address_ranges = {
    VkDeviceAddressRangeEXT{ .address = init.gpu_allocator.get_buffer_device_address(*bufferA), .size = buffer_size },
    VkDeviceAddressRangeEXT{ .address = init.gpu_allocator.get_buffer_device_address(*bufferB), .size = buffer_size },
    VkDeviceAddressRangeEXT{
      .address = init.gpu_allocator.get_buffer_device_address(*bufferResult), .size = buffer_size },
  };

  for (size_t i = 0; i < 3; ++i) {
    if (!write_storage_buffer_descriptor(init,
          address_ranges.at(i).address,
          address_ranges.at(i).size,
          std::span{ descriptor_data }.subspan(i * descriptor_stride, descriptor_size))) {
      std::println("Nie udało się zapisać deskryptora {}!", i);
      return;
    }
  }

  if (!init.gpu_allocator.write_buffer<std::byte>(*bufferHeap, std::span{ descriptor_data })) {
    std::println("Nie udało się przesłać sterty deskryptorów na GPU!");
    return;
  }

  auto const comp_code = read_file(std::string(EXAMPLE_SOURCE_DIRECTORY) + "/shaders/1plus1.comp.spv");
  VkShaderModule comp_module = create_shader_module(init, comp_code);
  if (comp_module == VK_NULL_HANDLE) {
    std::println("Nie udało się utworzyć modułu shadera!");
    return;
  }

  std::array<VkDescriptorSetAndBindingMappingEXT, 1> mappings = { VkDescriptorSetAndBindingMappingEXT{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT,
    .pNext = nullptr,
    .descriptorSet = 0,
    .firstBinding = 0,
    .bindingCount = 3,
    .resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT
                    | VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
    .source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT,
    .sourceData = { .constantOffset = { .heapOffset = 0,
                      .heapArrayStride = static_cast<uint32_t>(descriptor_stride),
                      .pEmbeddedSampler = nullptr,
                      .samplerHeapOffset = 0,
                      .samplerHeapArrayStride = 0 } },
  } };

  VkShaderDescriptorSetAndBindingMappingInfoEXT mapping_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
    .pNext = nullptr,
    .mappingCount = static_cast<uint32_t>(mappings.size()),
    .pMappings = mappings.data(),
  };

  VkPipelineShaderStageCreateInfo const comp_stage =
    initializers::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, comp_module, "main");
  VkPipelineShaderStageCreateInfo stage = comp_stage;
  stage.pNext = &mapping_info;

  VkPipelineCreateFlags2CreateInfo pipeline_flags = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
    .pNext = nullptr,
    .flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT,
  };

  VkComputePipelineCreateInfo const pipeline_info = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = &pipeline_flags,
    .flags = 0,
    .stage = stage,
    .layout = VK_NULL_HANDLE,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = -1,
  };

  VkPipeline compute_pipeline = VK_NULL_HANDLE;
  if (init.disp.createComputePipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &compute_pipeline) != VK_SUCCESS) {
    std::println("Nie udało się utworzyć potoku obliczeniowego!");
    init.disp.destroyShaderModule(comp_module, nullptr);
    return;
  }

  auto const compute_queue_index_res = init.device.get_queue_index(vkb::QueueType::compute);
  if (!compute_queue_index_res) {
    std::println("Brak indeksu kolejki obliczeniowej!");
    init.disp.destroyPipeline(compute_pipeline, nullptr);
    init.disp.destroyShaderModule(comp_module, nullptr);
    return;
  }

  auto const cmd_pool_info = initializers::CommandPoolCreateInfo(compute_queue_index_res.value());

  VkCommandPool command_pool = VK_NULL_HANDLE;
  if (init.disp.createCommandPool(&cmd_pool_info, nullptr, &command_pool) != VK_SUCCESS) {
    std::println("Nie udało się utworzyć puli komend!");
    init.disp.destroyPipeline(compute_pipeline, nullptr);
    init.disp.destroyShaderModule(comp_module, nullptr);
    return;
  }

  auto const cmd_alloc_info =
    initializers::CommandBufferAllocateInfo(command_pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  if (init.disp.allocateCommandBuffers(&cmd_alloc_info, &command_buffer) != VK_SUCCESS) {
    std::println("Nie udało się zaalokować bufora komend!");
    init.disp.destroyCommandPool(command_pool, nullptr);
    init.disp.destroyPipeline(compute_pipeline, nullptr);
    init.disp.destroyShaderModule(comp_module, nullptr);
    return;
  }

  VkDeviceAddress const heap_address = init.gpu_allocator.get_buffer_device_address(*bufferHeap);
  VkBindHeapInfoEXT const bind_heap_info = {
    .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
    .pNext = nullptr,
    .heapRange = { .address = heap_address, .size = heap_size },
    .reservedRangeOffset = reserved_range_offset,
    .reservedRangeSize = reserved_range_size,
  };

  auto const begin_info = initializers::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  init.disp.beginCommandBuffer(command_buffer, &begin_info);
  init.cmd_bind_resource_heap(command_buffer, &bind_heap_info);
  init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
  init.disp.cmdDispatch(command_buffer, element_count / 64, 1, 1);// NOLINT
  init.disp.endCommandBuffer(command_buffer);

  auto const submit_info = initializers::SubmitInfo({}, {}, std::span{ &command_buffer, 1 }, {});

  init.disp.queueSubmit(compute_queue, 1, &submit_info, VK_NULL_HANDLE);
  init.disp.queueWaitIdle(compute_queue);

  auto output = init.gpu_allocator.read_buffer<float>(*bufferResult, element_count);
  if (!output) {
    std::println("Nie udało się odczytać bufora wynikowego!");
    init.disp.destroyCommandPool(command_pool, nullptr);
    init.disp.destroyPipeline(compute_pipeline, nullptr);
    init.disp.destroyShaderModule(comp_module, nullptr);
    return;
  }

  std::println("Wyniki dodawania (pierwsze 5 z 1024):");
  for (size_t i{}; i < 5; i++) {// NOLINT
    std::println("Index {}: {} + {} = {}", i, input[i], input[i], output->at(i));// NOLINT
  }

  init.disp.destroyShaderModule(comp_module, nullptr);
  init.disp.destroyPipeline(compute_pipeline, nullptr);
  init.disp.destroyCommandPool(command_pool, nullptr);

  init.gpu_allocator.destroy_buffer(*bufferA);
  init.gpu_allocator.destroy_buffer(*bufferB);
  init.gpu_allocator.destroy_buffer(*bufferResult);
  init.gpu_allocator.destroy_buffer(*bufferHeap);

  std::println("--- Test dodawania dwóch tablic zakończony ---");
}

}// namespace vkgsplat::compute
