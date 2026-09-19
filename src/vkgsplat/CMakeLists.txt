include(GenerateExportHeader)

add_library(
  vkgsplat
  error.cpp
  engine.cpp
  renderer.cpp
  lfd_view_order.cpp)

add_library(vkgsplat::vkgsplat ALIAS vkgsplat)

target_link_libraries(
  vkgsplat
  PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings magic_enum::magic_enum
  PUBLIC vkgsplat::vkgsplat_backend
         vkgsplat::vkgsplat_utility
         vkgsplat::vkgsplat_window
         vkgsplat::vkgsplat_io
         glm::glm
         beman::indirect)

vkgsplat_configure_linker(vkgsplat)

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

# Must match vkgsplat_backend shader output; used by example_config.h → kShaderDirectory.
set(SHADER_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
configure_file(${PROJECT_SOURCE_DIR}/include/vkgsplat/example_config.h.in
               ${PROJECT_BINARY_DIR}/include/vkgsplat/example_config.h)

target_include_directories(
  vkgsplat ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
