include(GenerateExportHeader)

add_library(vkgsplat
  renderer.cpp
  mesh_gpu.cpp
  window.cpp
  backend/vulkan/device.cpp
  shader.cpp
  io/ply/miniply.cpp
  io/ply/load_splats.cpp
  backend/vulkan/depth_buffer.cpp
  backend/vulkan/graphics_pipeline.cpp
  backend/vulkan/swapchain.cpp
  backend/vulkan/command/pool.cpp
  backend/vulkan/renderer.cpp
  backend/vulkan/sync_objects/fence.cpp
  backend/vulkan/sync_objects/semaphore.cpp
  sphere_setup.cpp
  project_gaussians.cpp
  vma_usage.cpp
  backend/vulkan/gpu_allocator.cpp
  compute/algorithm.cpp
  compute/op_algo_dispatch.cpp
  compute/op_tensor_sync_device.cpp
  compute/op_tensor_sync_local.cpp
  backend/vulkan/descriptor/descriptor_heap.cpp
)

add_library(vkgsplat::vkgsplat ALIAS vkgsplat)

include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

set(SHADER_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

vkgsplat_compile_shaders(
  TARGET vkgsplat
  OUTPUT_DIR "${SHADER_DIRECTORY}"
  SOURCES
    shaders/sphere.vert
    shaders/sphere.frag
    shaders/init_spheres.comp
    shaders/project_gaussians.comp
)

target_link_libraries(
  vkgsplat
  PRIVATE vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings
          glfw::glfw
          Vulkan::Headers
          Vulkan::Vulkan
          vk-bootstrap::vk-bootstrap
          GPUOpen::VulkanMemoryAllocator
  PUBLIC glm::glm
)

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
  vkgsplat
  ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ply_load_test ply_load_test.cpp io/ply/miniply.cpp)
target_include_directories(ply_load_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  ply_load_test
  PRIVATE vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings)
target_compile_definitions(ply_load_test PRIVATE VKGSPLAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
vkgsplat_configure_linker(ply_load_test)
