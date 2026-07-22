include(GenerateExportHeader)

add_library(vkgsplat_utility error.cpp)

add_library(vkgsplat::vkgsplat_utility ALIAS vkgsplat_utility)

target_link_libraries(vkgsplat_utility PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings)

target_compile_features(vkgsplat_utility PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

vkgsplat_configure_linker(vkgsplat_utility)

set_target_properties(
  vkgsplat_utility
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)

generate_export_header(vkgsplat_utility EXPORT_FILE_NAME
                       ${PROJECT_BINARY_DIR}/include/vkgsplat_utility/vkgsplat_utility_export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat_utility PUBLIC VKGSPLAT_UTILITY_STATIC_DEFINE)
endif()

target_include_directories(
  vkgsplat_utility ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
