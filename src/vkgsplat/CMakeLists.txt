include(GenerateExportHeader)

add_library(
  vkgsplat
  error.cpp
  engine.cpp
  renderer.cpp
  io/ply/miniply.cpp
  io/ply/load_splats.cpp
  backend/vulkan/vma_usage.cpp
  backend/vulkan/vulkan_platform.cpp
  backend/vulkan/vulkan_driver.cpp
  backend/vulkan/presentable_swapchain.cpp
  backend/vulkan/device.cpp
  backend/vulkan/shader.cpp
  backend/vulkan/mesh_gpu.cpp
  backend/vulkan/sphere_setup.cpp
  backend/vulkan/depth_buffer.cpp
  backend/vulkan/graphics_pipeline.cpp
  backend/vulkan/swapchain_resource.cpp
  backend/vulkan/command/pool.cpp
  backend/vulkan/renderer.cpp
  backend/vulkan/sync_objects/barrier.cpp
  backend/vulkan/sync_objects/fence.cpp
  backend/vulkan/sync_objects/semaphore.cpp
  backend/vulkan/gs/projection.cpp
  backend/vulkan/gs/binning.cpp
  backend/vulkan/gs/sorting.cpp
  backend/vulkan/gs/rasterization.cpp
  backend/vulkan/gs/pipeline.cpp
  backend/vulkan/gs/operations.cpp
  backend/vulkan/gpu_allocator.cpp
  backend/vulkan/compute/algorithm.cpp
  backend/vulkan/compute/op_algo_dispatch.cpp
  backend/vulkan/compute/op_fill_buffer.cpp
  backend/vulkan/compute/op_tensor_sync_device.cpp
  backend/vulkan/compute/op_tensor_sync_local.cpp
  backend/vulkan/descriptor/descriptor_heap.cpp
  backend/vulkan/gpu_pass_timer.cpp
  backend/vulkan/imgui_overlay.cpp)

add_library(vkgsplat::vkgsplat ALIAS vkgsplat)

include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

set(SHADER_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

vkgsplat_compile_shaders(
  TARGET
  vkgsplat
  OUTPUT_DIR
  "${SHADER_DIRECTORY}"
  SOURCES
  shaders/sphere.vert
  shaders/sphere.frag
  shaders/init_spheres.comp
  shaders/gs/projection.comp
  shaders/gs/binning.comp
  shaders/gs/prepare_sorting.comp
  "${VKGSPLAT_VKRADIXSORT_SHADERS_DIR}/multi_radixsort_histograms.comp"
  "${VKGSPLAT_VKRADIXSORT_SHADERS_DIR}/multi_radixsort.comp"
  shaders/gs/identify_ranges.comp
  shaders/gs/rasterization.comp)

target_link_libraries(
  vkgsplat
  PRIVATE vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings
          Vulkan::Headers
          Vulkan::Vulkan
          vk-bootstrap::vk-bootstrap
          GPUOpen::VulkanMemoryAllocator
          magic_enum::magic_enum
          imgui::imgui
          glfw::glfw
  PUBLIC vkgsplat::vkgsplat_utility
         vkgsplat::vkgsplat_window
         glm::glm
         beman::indirect)

vkgsplat_configure_linker(vkgsplat)

target_compile_definitions(vkgsplat PRIVATE VKGSPLAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_compile_features(vkgsplat PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

set_target_properties(
  vkgsplat
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)

generate_export_header(vkgsplat EXPORT_FILE_NAME ${PROJECT_BINARY_DIR}/include/vkgsplat/vkgsplat_export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat PUBLIC VKGSPLAT_STATIC_DEFINE)
endif()

configure_file(${PROJECT_SOURCE_DIR}/include/vkgsplat/example_config.h.in
               ${PROJECT_BINARY_DIR}/include/vkgsplat/example_config.h)

target_include_directories(
  vkgsplat ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/backend/vulkan)

add_executable(ply_load_test ply_load_test.cpp io/ply/miniply.cpp)
target_include_directories(ply_load_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ply_load_test PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings vkgsplat::vkgsplat_utility)
target_compile_definitions(ply_load_test PRIVATE VKGSPLAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
vkgsplat_configure_linker(ply_load_test)
