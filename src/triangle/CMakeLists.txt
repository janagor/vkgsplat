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

configure_file(${PROJECT_SOURCE_DIR}/include/vkgsplat/example_config.h.in
               ${PROJECT_BINARY_DIR}/include/vkgsplat/example_config.h)

target_include_directories(triangle ${WARNING_GUARD} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

# Apply WASM-specific configuration
# if(EMSCRIPTEN)
#   vkgsplat_configure_wasm_target(intro
#     TITLE "Intro - FTXUI Sample"
#     DESCRIPTION "FTXUI-based terminal application"
#   )
# endif()

