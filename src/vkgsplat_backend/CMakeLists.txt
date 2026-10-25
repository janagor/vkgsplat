include(GenerateExportHeader)

add_library(
  vkgsplat_backend
  vulkan/vulkan_platform.cpp
  vulkan/vulkan_driver.cpp
  vulkan/presentable_swapchain.cpp
  vulkan/device.cpp
  vulkan/video/video_capabilities.cpp
  vulkan/video/video_device.cpp
  vulkan/video/video_session.cpp
  vulkan/video/video_encoder.cpp
  vulkan/video/color_converter.cpp
  vulkan/video/h264_gop.cpp
  vulkan/shader.cpp
  vulkan/mesh_gpu.cpp
  vulkan/sphere_setup.cpp
  vulkan/gs/graphics/graphics_pipeline.cpp
  vulkan/swapchain_resource.cpp
  vulkan/command/pool.cpp
  vulkan/renderer.cpp
  vulkan/frame_context.cpp
  vulkan/render_thread.cpp
  vulkan/present_pacer.cpp
  vulkan/gs/projection.cpp
  vulkan/gs/graphics/binning.cpp
  vulkan/gs/graphics/sorting.cpp
  vulkan/gs/rasterization.cpp
  vulkan/gs/pipeline.cpp
  vulkan/gs/operations.cpp
  vulkan/gs/tiled_compute/resources.cpp
  vulkan/gs/tiled_compute/operations.cpp
  vulkan/capture/readback.cpp
  vulkan/descriptor/descriptor_heap.cpp
  vulkan/gpu_pass_timer.cpp
  vulkan/imgui_overlay.cpp
  vulkan/vkexec_requirements.cpp)

add_library(vkgsplat::vkgsplat_backend ALIAS vkgsplat_backend)

include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

set(SHADER_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

vkgsplat_compile_shaders(
  TARGET
  vkgsplat_backend
  OUTPUT_DIR
  "${SHADER_DIRECTORY}"
  SOURCES
  shaders/gs/graphics/sphere.vert
  shaders/gs/graphics/sphere.frag
  shaders/gs/projection.comp
  shaders/gs/projection_batch.comp
  shaders/gs/graphics/binning.comp
  shaders/gs/graphics/prepare_sorting.comp
  "${VKGSPLAT_VKRADIXSORT_SHADERS_DIR}/multi_radixsort_histograms.comp"
  "${VKGSPLAT_VKRADIXSORT_SHADERS_DIR}/multi_radixsort.comp"
  shaders/gs/graphics/identify_ranges.comp
  shaders/gs/tiled_compute/rasterization.comp
  shaders/gs/tiled_compute/tiled_count.comp
  shaders/gs/tiled_compute/tiled_scan.comp
  shaders/gs/tiled_compute/tiled_emit.comp
  shaders/gs/tiled_compute/tiled_prepare.comp
  shaders/gs/tiled_compute/tiled_radix_histograms.comp
  shaders/gs/tiled_compute/tiled_radix_prefix.comp
  shaders/gs/tiled_compute/tiled_radix_scatter.comp
  shaders/gs/tiled_compute/tiled_identify_ranges.comp
  shaders/gs/tiled_compute/tiled_rasterization.comp
  shaders/video/rgba_to_nv12.comp)

target_link_libraries(
  vkgsplat_backend
  PRIVATE vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings
          Vulkan::Vulkan
          magic_enum::magic_enum
  PUBLIC Vulkan::Headers
         vk-bootstrap::vk-bootstrap
         GPUOpen::VulkanMemoryAllocator
         vkexec::vkexec
         vkexec::vma
         vkexec::ext_descriptor_heap
         vkexec::ext_timeline_semaphore
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
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vulkan>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/vulkan)
