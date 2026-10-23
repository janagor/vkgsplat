add_executable(vkgsplat_headless main.cpp)
target_link_libraries(
  vkgsplat_headless
  PRIVATE vkgsplat::vkgsplat
          vkgsplat::vkgsplat_animation
          vkgsplat::vkgsplat_capture
          vkgsplat::vkgsplat_headless_platform
          vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings
          CLI11::CLI11)
vkgsplat_configure_linker(vkgsplat_headless)
