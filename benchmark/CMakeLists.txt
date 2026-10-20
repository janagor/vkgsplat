add_subdirectory(fixture)

if(vkgsplat_BUILD_BENCHMARKS)
  find_package(Git REQUIRED)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE benchmark_git_sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE benchmark_git_result)
  if(NOT
     benchmark_git_result
     EQUAL
     0
     OR benchmark_git_sha STREQUAL "")
    message(FATAL_ERROR "Benchmark results require a Git revision")
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE benchmark_git_status
    RESULT_VARIABLE benchmark_git_status_result)
  if(NOT
     benchmark_git_status_result
     EQUAL
     0)
    message(FATAL_ERROR "Benchmark results require Git working-tree status")
  endif()
  if(benchmark_git_status STREQUAL "")
    set(benchmark_git_dirty "false")
  else()
    set(benchmark_git_dirty "true")
  endif()

  set(benchmark_scene "${CMAKE_CURRENT_BINARY_DIR}/baseline-v1.ply")
  add_custom_command(
    OUTPUT "${benchmark_scene}" "${benchmark_scene}.json"
    COMMAND $<TARGET_FILE:vkgsplat_fixture_gen> --profile baseline-v1 --output "${benchmark_scene}"
    DEPENDS vkgsplat_fixture_gen
    VERBATIM)
  add_custom_target(vkgsplat_benchmark_scene DEPENDS "${benchmark_scene}" "${benchmark_scene}.json")

  add_executable(
    vkgsplat_bench
    main.cpp
    render_throughput.cpp
    gpu_pass_timings.cpp
    gpu_statistics.cpp
    capture_sequence.cpp)
  add_dependencies(vkgsplat_bench vkgsplat_benchmark_scene)
  target_link_libraries(
    vkgsplat_bench
    PRIVATE benchmark::benchmark
            vkgsplat::vkgsplat_benchmark_renderer_adapter
            vkgsplat::vkgsplat_headless_platform
            vkgsplat::vkgsplat_capture
            vkgsplat::vkgsplat_options
            vkgsplat::vkgsplat_warnings)
  target_compile_features(vkgsplat_bench PRIVATE cxx_std_23)
  target_compile_definitions(
    vkgsplat_bench
    PRIVATE VKGSPLAT_BENCHMARK_SCENE="${benchmark_scene}"
            VKGSPLAT_BENCHMARK_GIT_SHA="${benchmark_git_sha}"
            VKGSPLAT_BENCHMARK_GIT_DIRTY="${benchmark_git_dirty}"
            VKGSPLAT_BENCHMARK_IPO="${vkgsplat_ENABLE_IPO}"
            VKGSPLAT_BENCHMARK_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
  vkgsplat_configure_linker(vkgsplat_bench)
endif()
