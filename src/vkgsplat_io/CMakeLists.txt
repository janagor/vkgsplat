include(GenerateExportHeader)

add_library(
  vkgsplat_io
  ply/miniply.cpp
  ply/load_splats.cpp
  image/write_png.cpp
  image/stb_image_write_impl.cpp)

add_library(vkgsplat::vkgsplat_io ALIAS vkgsplat_io)

# stb_image_write's implementation trips clang-analyzer false positives; keep it out of tidy/cppcheck.
set_source_files_properties(image/stb_image_write_impl.cpp PROPERTIES SKIP_LINTING ON)

target_link_libraries(
  vkgsplat_io
  PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings stb::stb
  PUBLIC vkgsplat::vkgsplat_utility)

target_compile_features(vkgsplat_io PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

vkgsplat_configure_linker(vkgsplat_io)

set_target_properties(
  vkgsplat_io
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)

generate_export_header(vkgsplat_io EXPORT_FILE_NAME ${PROJECT_BINARY_DIR}/include/vkgsplat_io/vkgsplat_io_export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat_io PUBLIC VKGSPLAT_IO_STATIC_DEFINE)
endif()

target_include_directories(
  vkgsplat_io ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ply_load_test ply_load_test.cpp ply/miniply.cpp)
target_include_directories(ply_load_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ply_load_test PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings vkgsplat::vkgsplat_utility)
target_compile_definitions(ply_load_test PRIVATE VKGSPLAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
vkgsplat_configure_linker(ply_load_test)
