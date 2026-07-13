add_executable(triangle
  main.cpp
  application.cpp
  mesh_gpu.cpp
  window.cpp
  backend/vulkan/device.cpp
  shader.cpp
  io/ply/miniply.cpp
  backend/vulkan/depth_buffer.cpp
  backend/vulkan/graphics_pipeline.cpp
  backend/vulkan/swapchain.cpp
  backend/vulkan/command/pool.cpp
  backend/vulkan/renderer.cpp
  backend/vulkan/sync_objects/fence.cpp
  backend/vulkan/sync_objects/semaphore.cpp
  sphere_setup.cpp
  vma_usage.cpp
  backend/vulkan/gpu_allocator.cpp
  compute/algorithm.cpp
  compute/param.cpp
  compute/op_algo_dispatch.cpp
  compute/op_tensor_sync_device.cpp
  compute/op_tensor_sync_local.cpp
  backend/vulkan/descriptor/descriptor_heap.cpp
)

include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

set(SHADER_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

vkgsplat_compile_shaders(
  TARGET triangle
  OUTPUT_DIR "${SHADER_DIRECTORY}"
  SOURCES
    shaders/sphere.vert
    shaders/sphere.frag
    shaders/init_spheres.comp
)

target_link_libraries(
  triangle
  PRIVATE vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings

          glfw::glfw
          Vulkan::Headers
          Vulkan::Vulkan
          vk-bootstrap::vk-bootstrap
          GPUOpen::VulkanMemoryAllocator
          glm::glm
)

vkgsplat_configure_linker(triangle)

add_executable(ply_load_test ply_load_test.cpp io/ply/miniply.cpp)
target_include_directories(ply_load_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  ply_load_test
  PRIVATE vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings)
target_compile_definitions(ply_load_test PRIVATE VKGSPLAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
vkgsplat_configure_linker(ply_load_test)

configure_file(${PROJECT_SOURCE_DIR}/include/vkgsplat/example_config.h.in
               ${PROJECT_BINARY_DIR}/include/vkgsplat/example_config.h)

target_include_directories(triangle ${WARNING_GUARD} PUBLIC  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                             $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
                                                     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Apply WASM-specific configuration
# if(EMSCRIPTEN)
#   vkgsplat_configure_wasm_target(intro
#     TITLE "Intro - FTXUI Sample"
#     DESCRIPTION "FTXUI-based terminal application"
#   )
# endif()
