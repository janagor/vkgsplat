include(GenerateExportHeader)

add_library(vkgsplat_input_handler input_handler.cpp key_bindings.cpp)

add_library(vkgsplat::vkgsplat_input_handler ALIAS vkgsplat_input_handler)

target_link_libraries(
  vkgsplat_input_handler
  PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings
  PUBLIC vkgsplat::vkgsplat_utility vkgsplat::vkgsplat_window glfw::glfw)

target_compile_features(vkgsplat_input_handler PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

vkgsplat_configure_linker(vkgsplat_input_handler)

set_target_properties(
  vkgsplat_input_handler
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)

generate_export_header(vkgsplat_input_handler EXPORT_FILE_NAME
                       ${PROJECT_BINARY_DIR}/include/vkgsplat_input_handler/vkgsplat_input_handler_export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat_input_handler PUBLIC VKGSPLAT_INPUT_HANDLER_STATIC_DEFINE)
endif()

target_include_directories(
  vkgsplat_input_handler ${WARNING_GUARD}
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
