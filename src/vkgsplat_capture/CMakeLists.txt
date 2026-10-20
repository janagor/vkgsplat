include(GenerateExportHeader)
find_package(Threads REQUIRED)

add_library(vkgsplat_capture screenshot.cpp frame_sequence.cpp png_sequence_writer.cpp)
add_library(vkgsplat::vkgsplat_capture ALIAS vkgsplat_capture)

target_link_libraries(
  vkgsplat_capture
  PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings vkgsplat::vkgsplat_io Threads::Threads
  PUBLIC vkgsplat::vkgsplat)

vkgsplat_configure_linker(vkgsplat_capture)
target_compile_features(vkgsplat_capture PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

set_target_properties(
  vkgsplat_capture
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)

generate_export_header(vkgsplat_capture EXPORT_FILE_NAME
                       ${PROJECT_BINARY_DIR}/include/vkgsplat_capture/vkgsplat_capture_export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat_capture PUBLIC VKGSPLAT_CAPTURE_STATIC_DEFINE)
endif()

target_include_directories(
  vkgsplat_capture ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
