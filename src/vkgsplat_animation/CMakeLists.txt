include(GenerateExportHeader)

add_library(vkgsplat_animation camera_path.cpp camera_path_json.cpp)
add_library(vkgsplat::vkgsplat_animation ALIAS vkgsplat_animation)
target_link_libraries(
  vkgsplat_animation
  PUBLIC vkgsplat::vkgsplat_utility glm::glm
  PRIVATE nlohmann_json::nlohmann_json vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings)
target_compile_features(vkgsplat_animation PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
vkgsplat_configure_linker(vkgsplat_animation)
set_target_properties(
  vkgsplat_animation
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)
generate_export_header(vkgsplat_animation EXPORT_FILE_NAME
                       ${PROJECT_BINARY_DIR}/include/vkgsplat_animation/vkgsplat_animation_export.hpp)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(vkgsplat_animation PUBLIC VKGSPLAT_ANIMATION_STATIC_DEFINE)
endif()
target_include_directories(vkgsplat_animation ${WARNING_GUARD} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                                      $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
