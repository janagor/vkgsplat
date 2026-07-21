add_executable(vkgsplat_app main.cpp application.cpp app_config.cpp)

target_link_libraries(vkgsplat_app PRIVATE vkgsplat::vkgsplat vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings)

vkgsplat_configure_linker(vkgsplat_app)

target_compile_definitions(vkgsplat_app PRIVATE VKGSPLAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_include_directories(vkgsplat_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
