add_executable(vkgsplat_app main.cpp application.cpp app_config.cpp)

target_link_libraries(
  vkgsplat_app
  PRIVATE vkgsplat::vkgsplat
          vkgsplat::vkgsplat_input_handler
          vkgsplat::vkgsplat_window
          vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings
          CLI11::CLI11)

vkgsplat_configure_linker(vkgsplat_app)

target_include_directories(vkgsplat_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
