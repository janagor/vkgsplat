include(GenerateExportHeader)

add_library(vkgsplat_window window.cpp)

add_library(vkgsplat::vkgsplat_window ALIAS vkgsplat_window)

target_link_libraries(
  vkgsplat_window
  PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings
  PUBLIC vkgsplat::vkgsplat_utility glfw::glfw Vulkan::Headers)

target_compile_features(vkgsplat_window PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

vkgsplat_configure_linker(vkgsplat_window)

set_target_properties(
  vkgsplat_window
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)

generate_export_header(vkgsplat_window EXPORT_FILE_NAME
                       ${PROJECT_BINARY_DIR}/include/vkgsplat_window/vkgsplat_window_export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat_window PUBLIC VKGSPLAT_WINDOW_STATIC_DEFINE)
endif()

target_include_directories(
  vkgsplat_window ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
