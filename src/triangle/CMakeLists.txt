add_executable(triangle
         main.cpp
         application.cpp
         mesh_gpu.cpp
         depth_buffer.cpp
         window.cpp
         device.cpp
         shader.cpp
         graphics_pipeline.cpp
         swapchain.cpp
         renderer.cpp
         sync_objects/fence.cpp
         sync_objects/semaphore.cpp
         sphere_setup.cpp
         vma_usage.cpp
         backend/vulkan/gpu_allocator.cpp
         compute/algorithm.cpp
         compute/param.cpp
         compute/op_algo_dispatch.cpp
         compute/op_tensor_sync_device.cpp
         compute/op_tensor_sync_local.cpp
         descriptor/descriptor_heap.cpp
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
)

vkgsplat_configure_linker(triangle)

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
