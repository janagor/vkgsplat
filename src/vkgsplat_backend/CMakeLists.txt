include(GenerateExportHeader)

add_library(
  vkgsplat_backend
  vulkan/vma_usage.cpp
  vulkan/vulkan_platform.cpp
  vulkan/vulkan_driver.cpp
  vulkan/presentable_swapchain.cpp
  vulkan/device.cpp
  vulkan/shader.cpp
  vulkan/mesh_gpu.cpp
  vulkan/sphere_setup.cpp
  vulkan/graphics_pipeline.cpp
  vulkan/swapchain_resource.cpp
  vulkan/command/pool.cpp
  vulkan/renderer.cpp
  vulkan/frame_context.cpp
  vulkan/render_thread.cpp
  vulkan/present_pacer.cpp
  vulkan/sync_objects/barrier.cpp
  vulkan/sync_objects/semaphore.cpp
  vulkan/gs/projection.cpp
  vulkan/gs/binning.cpp
  vulkan/gs/sorting.cpp
  vulkan/gs/rasterization.cpp
  vulkan/gs/pipeline.cpp
  vulkan/gs/operations.cpp
  vulkan/gpu_allocator.cpp
  vulkan/screenshot.cpp
  vulkan/compute/algorithm.cpp
  vulkan/compute/op_fill_buffer.cpp
  vulkan/compute/op_tensor_sync_device.cpp
  vulkan/compute/op_tensor_sync_local.cpp
  vulkan/descriptor/descriptor_heap.cpp
  vulkan/gpu_pass_timer.cpp
  vulkan/imgui_overlay.cpp)

add_library(vkgsplat::vkgsplat_backend ALIAS vkgsplat_backend)

include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

set(SHADER_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

vkgsplat_compile_shaders(
  TARGET
  vkgsplat_backend
  OUTPUT_DIR
  "${SHADER_DIRECTORY}"
  SOURCES
  shaders/sphere.vert
  shaders/sphere.frag
  shaders/gs/projection.comp
  shaders/gs/binning.comp
  shaders/gs/prepare_sorting.comp
  "${VKGSPLAT_VKRADIXSORT_SHADERS_DIR}/multi_radixsort_histograms.comp"
  "${VKGSPLAT_VKRADIXSORT_SHADERS_DIR}/multi_radixsort.comp"
  shaders/gs/identify_ranges.comp
  shaders/gs/rasterization.comp)

target_link_libraries(
  vkgsplat_backend
  PRIVATE vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings
          Vulkan::Vulkan
          magic_enum::magic_enum
  PUBLIC Vulkan::Headers
         vk-bootstrap::vk-bootstrap
         GPUOpen::VulkanMemoryAllocator
         glfw::glfw
         imgui::imgui
         vkgsplat::vkgsplat_utility
         vkgsplat::vkgsplat_adt
         vkgsplat::vkgsplat_window
         vkgsplat::vkgsplat_io
         glm::glm
         beman::indirect)

vkgsplat_configure_linker(vkgsplat_backend)

target_compile_definitions(vkgsplat_backend PRIVATE VKGSPLAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_compile_features(vkgsplat_backend PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

set_target_properties(
  vkgsplat_backend
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)

generate_export_header(vkgsplat_backend EXPORT_FILE_NAME
                       ${PROJECT_BINARY_DIR}/include/vkgsplat_backend/vkgsplat_backend_export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat_backend PUBLIC VKGSPLAT_BACKEND_STATIC_DEFINE)
endif()

# Propagate backend include roots so src/vkgsplat/{engine,renderer}.cpp can use
# "vulkan/..." and short "gs/..." / "app_state.hpp" includes.
target_include_directories(
  vkgsplat_backend ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vulkan>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/vulkan)
