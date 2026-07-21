add_executable(vkgsplat_renderer main.cpp application.cpp app_config.cpp)

target_link_libraries(vkgsplat_renderer PRIVATE vkgsplat::vkgsplat vkgsplat::vkgsplat_options
                                                vkgsplat::vkgsplat_warnings)

vkgsplat_configure_linker(vkgsplat_renderer)

target_compile_definitions(vkgsplat_renderer PRIVATE VKGSPLAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_include_directories(vkgsplat_renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
