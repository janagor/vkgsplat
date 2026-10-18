add_library(
  vkgsplat_benchmark_fixture
  scene.cpp
  ply_writer.cpp
  profile.cpp
  manifest.cpp)
add_library(vkgsplat::vkgsplat_benchmark_fixture ALIAS vkgsplat_benchmark_fixture)
target_include_directories(vkgsplat_benchmark_fixture PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(
  vkgsplat_benchmark_fixture
  PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings
  PUBLIC vkgsplat::vkgsplat_io glm::glm)
target_compile_features(vkgsplat_benchmark_fixture PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
vkgsplat_configure_linker(vkgsplat_benchmark_fixture)

add_library(vkgsplat_benchmark_renderer_adapter renderer_adapter.cpp)
add_library(vkgsplat::vkgsplat_benchmark_renderer_adapter ALIAS vkgsplat_benchmark_renderer_adapter)
target_include_directories(vkgsplat_benchmark_renderer_adapter PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(
  vkgsplat_benchmark_renderer_adapter
  PRIVATE vkgsplat::vkgsplat_options vkgsplat::vkgsplat_warnings
  PUBLIC vkgsplat::vkgsplat_benchmark_fixture vkgsplat::vkgsplat)
vkgsplat_configure_linker(vkgsplat_benchmark_renderer_adapter)

add_executable(vkgsplat_fixture_gen main.cpp)
target_link_libraries(
  vkgsplat_fixture_gen
  PRIVATE vkgsplat::vkgsplat_benchmark_fixture
          CLI11::CLI11
          vkgsplat::vkgsplat_options
          vkgsplat::vkgsplat_warnings)
vkgsplat_configure_linker(vkgsplat_fixture_gen)
