include(GenerateExportHeader)

add_library(vkgsplat_headless_platform headless_platform.cpp offscreen_platform.cpp)
add_library(vkgsplat::vkgsplat_headless_platform ALIAS vkgsplat_headless_platform)
target_link_libraries(
  vkgsplat_headless_platform
  PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings
  PUBLIC vkgsplat::vkgsplat_utility)
target_compile_features(vkgsplat_headless_platform PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
vkgsplat_configure_linker(vkgsplat_headless_platform)
set_target_properties(
  vkgsplat_headless_platform
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)
generate_export_header(vkgsplat_headless_platform EXPORT_FILE_NAME
                       ${PROJECT_BINARY_DIR}/include/vkgsplat_headless/vkgsplat_headless_platform_export.hpp)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat_headless_platform PUBLIC VKGSPLAT_HEADLESS_PLATFORM_STATIC_DEFINE)
endif()
target_include_directories(
  vkgsplat_headless_platform ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
