add_executable(triangle main.cpp)

target_link_libraries(
  triangle
  PRIVATE vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings

          glfw::glfw
          Vulkan::Headers
          vk-bootstrap::vk-bootstrap
)

vkgsplat_configure_linker(triangle)

configure_file (
    "${PROJECT_SOURCE_DIR}/src/triangle/example_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/example_config.h"
)

target_include_directories(triangle PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include"
"${CMAKE_CURRENT_BINARY_DIR}")

# Apply WASM-specific configuration
# if(EMSCRIPTEN)
#   vkgsplat_configure_wasm_target(intro
#     TITLE "Intro - FTXUI Sample"
#     DESCRIPTION "FTXUI-based terminal application"
#   )
# endif()

