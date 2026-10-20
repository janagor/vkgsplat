cmake_minimum_required(VERSION 3.15...3.23)

project(
  CmakeConfigPackageTests
  VERSION ${PROJECT_VERSION}
  LANGUAGES CXX)

# Workaround clang>=22 flagging __COUNTER__ as c2y-extension
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 22)
  add_definitions(-DCATCH_CONFIG_NO_COUNTER)
endif()

# ---- Test as standalone project the exported config package ----

if(PROJECT_IS_TOP_LEVEL OR TEST_INSTALLED_VERSION)
  enable_testing()

  find_package(vkgsplat CONFIG REQUIRED)

  if(NOT TARGET vkgsplat_options)
    message(FATAL_ERROR "Required config package not found!")
    return() # be strictly paranoid for Template Janitor github action! CK
  endif()
endif()

# ---- Dependencies ----

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

if(TARGET vkgsplat::vkgsplat_utility)
  add_executable(vkgsplat_gpu_timing_statistics_test gpu_timing_statistics.cpp ../benchmark/gpu_statistics.cpp)
  target_link_libraries(vkgsplat_gpu_timing_statistics_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_utility)
  target_include_directories(vkgsplat_gpu_timing_statistics_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
  catch_discover_tests(vkgsplat_gpu_timing_statistics_test)

  if(TARGET vkgsplat::vkgsplat_benchmark_fixture)
    add_executable(vkgsplat_fixture_test fixture.cpp)
    target_link_libraries(vkgsplat_fixture_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_benchmark_fixture)
    catch_discover_tests(vkgsplat_fixture_test)

    add_executable(vkgsplat_fixture_adapter_test fixture_adapter.cpp)
    target_link_libraries(vkgsplat_fixture_adapter_test PRIVATE Catch2::Catch2WithMain
                                                                vkgsplat::vkgsplat_benchmark_renderer_adapter)
    catch_discover_tests(vkgsplat_fixture_adapter_test)
  endif()
  add_executable(vkgsplat_movement_clock_test movement_clock.cpp)
  target_link_libraries(vkgsplat_movement_clock_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_utility glm::glm)
  catch_discover_tests(vkgsplat_movement_clock_test)

  add_executable(vkgsplat_latest_frame_mailbox_test latest_frame_mailbox.cpp)
  target_link_libraries(vkgsplat_latest_frame_mailbox_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_utility)
  catch_discover_tests(vkgsplat_latest_frame_mailbox_test)

  add_executable(vkgsplat_window_worker_wakeup_test window_worker_wakeup.cpp)
  target_link_libraries(vkgsplat_window_worker_wakeup_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_window)
  catch_discover_tests(
    vkgsplat_window_worker_wakeup_test
    PROPERTIES
    TIMEOUT
    5)

  if(TARGET vkgsplat::vkgsplat_headless_platform)
    add_executable(vkgsplat_headless_platform_test headless_platform.cpp)
    target_link_libraries(vkgsplat_headless_platform_test PRIVATE Catch2::Catch2WithMain
                                                                  vkgsplat::vkgsplat_headless_platform)
    catch_discover_tests(vkgsplat_headless_platform_test)
  endif()

  if(TARGET vkgsplat::vkgsplat_backend)
    add_executable(vkgsplat_h264_writer_test h264_writer.cpp)
    target_link_libraries(vkgsplat_h264_writer_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_io)
    catch_discover_tests(vkgsplat_h264_writer_test)

    add_executable(vkgsplat_video_capabilities_test video_capabilities.cpp)
    target_link_libraries(vkgsplat_video_capabilities_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_backend)
    catch_discover_tests(vkgsplat_video_capabilities_test)

    add_executable(vkgsplat_capture_png_test capture_png.cpp)
    target_link_libraries(vkgsplat_capture_png_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_capture stb::stb)
    catch_discover_tests(vkgsplat_capture_png_test)

    add_executable(vkgsplat_png_sequence_writer_test png_sequence_writer.cpp ../src/vkgsplat_capture/png_sequence_writer.cpp)
    target_link_libraries(vkgsplat_png_sequence_writer_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_capture stb::stb)
    catch_discover_tests(
      vkgsplat_png_sequence_writer_test
      PROPERTIES
      TIMEOUT
      10)

    add_executable(vkgsplat_render_thread_test render_thread.cpp)
    target_link_libraries(vkgsplat_render_thread_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_backend
                                                              vkgsplat::vkgsplat)
    catch_discover_tests(
      vkgsplat_render_thread_test
      PROPERTIES
      TIMEOUT
      10)

    add_executable(vkgsplat_vulkan_surface_test vulkan_surface.cpp)
    target_link_libraries(vkgsplat_vulkan_surface_test PRIVATE Catch2::Catch2WithMain vkgsplat::vkgsplat_backend)
    catch_discover_tests(vkgsplat_vulkan_surface_test)
  endif()
endif()

option(vkgsplat_ENABLE_HEADLESS_GPU_TESTS "Enable GPU-dependent headless integration tests" OFF)
option(vkgsplat_HEADLESS_GPU_TEST_VALIDATION "Enable validation layers in the headless GPU test" ON)
option(vkgsplat_ENABLE_VIDEO_GPU_TESTS "Enable Vulkan Video integration tests without a scene fixture" OFF)
set(vkgsplat_HEADLESS_TEST_PLY
    ""
    CACHE FILEPATH "PLY fixture for headless GPU integration test")
if(TARGET vkgsplat_headless)
  add_test(NAME headless.cli.arguments
           COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:vkgsplat_headless>
                   -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_cli.cmake)
endif()
if(vkgsplat_ENABLE_VIDEO_GPU_TESTS OR vkgsplat_ENABLE_HEADLESS_GPU_TESTS)
  if(NOT TARGET vkgsplat::vkgsplat_backend OR NOT TARGET vkgsplat::vkgsplat_headless_platform)
    message(FATAL_ERROR "Vulkan Video tests require a native Vulkan backend and offscreen platform")
  endif()
  add_executable(vkgsplat_video_session_test video_session.cpp)
  target_link_libraries(vkgsplat_video_session_test PRIVATE vkgsplat::vkgsplat_backend
                                                           vkgsplat::vkgsplat_headless_platform)
  target_compile_definitions(vkgsplat_video_session_test PRIVATE
                             VKGSPLAT_TEST_VIDEO_VALIDATION=$<BOOL:${vkgsplat_HEADLESS_GPU_TEST_VALIDATION}>)
  add_test(NAME video.session.parameters COMMAND $<TARGET_FILE:vkgsplat_video_session_test>)
  set_tests_properties(video.session.parameters PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  add_executable(vkgsplat_video_encode_synthetic_test video_encode_synthetic.cpp)
  target_link_libraries(vkgsplat_video_encode_synthetic_test PRIVATE vkgsplat::vkgsplat_backend
                                                                    vkgsplat::vkgsplat_headless_platform
                                                                    vkgsplat::vkgsplat_io)
  target_compile_definitions(vkgsplat_video_encode_synthetic_test PRIVATE
                             VKGSPLAT_TEST_VIDEO_VALIDATION=$<BOOL:${vkgsplat_HEADLESS_GPU_TEST_VALIDATION}>)
  find_program(vkgsplat_FFMPEG_EXECUTABLE ffmpeg)
  find_program(vkgsplat_FFPROBE_EXECUTABLE ffprobe)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  add_test(NAME video.encode.synthetic.smoke
           COMMAND $<TARGET_FILE:vkgsplat_video_encode_synthetic_test>
                   ${CMAKE_CURRENT_BINARY_DIR}/video-synthetic-smoke.h264)
  set_tests_properties(video.encode.synthetic.smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  if(vkgsplat_FFMPEG_EXECUTABLE AND vkgsplat_FFPROBE_EXECUTABLE AND Python3_Interpreter_FOUND)
    add_test(NAME video.encode.synthetic
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/video_encode_integration.py
                     $<TARGET_FILE:vkgsplat_video_encode_synthetic_test> ${CMAKE_COMMAND}
                     ${CMAKE_CURRENT_SOURCE_DIR}/video_encode_verify.cmake
                     ${vkgsplat_FFMPEG_EXECUTABLE} ${vkgsplat_FFPROBE_EXECUTABLE})
    set_tests_properties(video.encode.synthetic PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  elseif(vkgsplat_ENABLE_VIDEO_GPU_TESTS)
    message(STATUS "Full Vulkan Video encode verification requires FFmpeg, FFprobe, and Python3")
  endif()
endif()
if(vkgsplat_ENABLE_HEADLESS_GPU_TESTS)
  if(NOT TARGET vkgsplat_headless OR NOT EXISTS "${vkgsplat_HEADLESS_TEST_PLY}")
    message(FATAL_ERROR "Headless GPU tests require a native build and vkgsplat_HEADLESS_TEST_PLY")
  endif()
  add_executable(vkgsplat_video_device_test video_device.cpp)
  target_link_libraries(vkgsplat_video_device_test PRIVATE vkgsplat::vkgsplat vkgsplat::vkgsplat_headless_platform)
  add_test(NAME video.device.initialization COMMAND $<TARGET_FILE:vkgsplat_video_device_test>
                                                    ${vkgsplat_HEADLESS_TEST_PLY})
  set_tests_properties(video.device.initialization PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  add_test(
    NAME headless.gpu.smoke
    COMMAND
      ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:vkgsplat_headless> -DPLY=${vkgsplat_HEADLESS_TEST_PLY}
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/headless-test.png -DVALIDATION=${vkgsplat_HEADLESS_GPU_TEST_VALIDATION} -P
      ${CMAKE_CURRENT_SOURCE_DIR}/headless_gpu_smoke.cmake)
  set_tests_properties(
    headless.gpu.smoke
    PROPERTIES ENVIRONMENT
               "DISPLAY=;WAYLAND_DISPLAY="
               TIMEOUT
               120)
  add_test(
    NAME offscreen.gpu.smoke
    COMMAND
      ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:vkgsplat_headless> -DPLY=${vkgsplat_HEADLESS_TEST_PLY}
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/offscreen-test.png -DTARGET_MODE=offscreen
      -DVALIDATION=${vkgsplat_HEADLESS_GPU_TEST_VALIDATION} -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_gpu_smoke.cmake)
  set_tests_properties(
    offscreen.gpu.smoke
    PROPERTIES ENVIRONMENT
               "DISPLAY=;WAYLAND_DISPLAY="
               TIMEOUT
               120)
  if(TARGET vkgsplat_fixture_gen)
    add_executable(vkgsplat_gpu_timing_smoke gpu_timing_smoke.cpp)
    target_link_libraries(vkgsplat_gpu_timing_smoke PRIVATE vkgsplat::vkgsplat_benchmark_renderer_adapter
                                                            vkgsplat::vkgsplat_headless_platform)
    add_executable(vkgsplat_fixture_gpu_smoke fixture_gpu_smoke.cpp)
    target_link_libraries(vkgsplat_fixture_gpu_smoke PRIVATE vkgsplat::vkgsplat_benchmark_renderer_adapter
                                                             vkgsplat::vkgsplat_headless_platform vkgsplat::vkgsplat_capture stb::stb)
    add_executable(vkgsplat_png_sequence_verify png_sequence_verify.cpp)
    target_link_libraries(vkgsplat_png_sequence_verify PRIVATE stb::stb vkgsplat::vkgsplat_utility)
    set(benchmark_scene ${CMAKE_CURRENT_BINARY_DIR}/benchmark-baseline-v1.ply)
    add_executable(vkgsplat_offscreen_gpu_smoke offscreen_gpu_smoke.cpp)
    target_link_libraries(vkgsplat_offscreen_gpu_smoke PRIVATE vkgsplat::vkgsplat vkgsplat::vkgsplat_headless_platform
                                                               vkgsplat::vkgsplat_benchmark_fixture vkgsplat::vkgsplat_capture stb::stb)
    add_test(
      NAME headless.gpu.sequence
      COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:vkgsplat_headless>
              -DVERIFY_EXECUTABLE=$<TARGET_FILE:vkgsplat_png_sequence_verify>
              -DPLY=${benchmark_scene}
              -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/headless-sequence-test
              -DVALIDATION=${vkgsplat_HEADLESS_GPU_TEST_VALIDATION}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_sequence_gpu_smoke.cmake)
    set_tests_properties(
      headless.gpu.sequence
      PROPERTIES FIXTURES_REQUIRED
                 benchmark_fixture
                 ENVIRONMENT
                 "DISPLAY=;WAYLAND_DISPLAY="
                 TIMEOUT
                 180)
    add_test(NAME offscreen.gpu.sequence
             COMMAND $<TARGET_FILE:vkgsplat_offscreen_gpu_smoke> ${benchmark_scene}
                     ${CMAKE_CURRENT_BINARY_DIR}/offscreen-compact.ply ${vkgsplat_HEADLESS_GPU_TEST_VALIDATION})
    set_tests_properties(
      offscreen.gpu.sequence
      PROPERTIES FIXTURES_REQUIRED
                 benchmark_fixture
                 ENVIRONMENT
                 "DISPLAY=;WAYLAND_DISPLAY="
                 TIMEOUT
                 180)
    add_test(NAME benchmark.fixture.generate COMMAND $<TARGET_FILE:vkgsplat_fixture_gen> --profile baseline-v1 --output
                                                     ${benchmark_scene})
    set_tests_properties(
      benchmark.fixture.generate
      PROPERTIES FIXTURES_SETUP
                 benchmark_fixture
                 TIMEOUT
                 180)
    add_test(NAME benchmark.fixture.verify COMMAND $<TARGET_FILE:vkgsplat_fixture_gen> --verify ${benchmark_scene})
    set_tests_properties(
      benchmark.fixture.verify
      PROPERTIES FIXTURES_REQUIRED
                 benchmark_fixture
                 TIMEOUT
                 30)
    add_test(NAME benchmark.gpu.timing.smoke COMMAND $<TARGET_FILE:vkgsplat_gpu_timing_smoke> ${benchmark_scene})
    set_tests_properties(
      benchmark.gpu.timing.smoke
      PROPERTIES FIXTURES_REQUIRED
                 benchmark_fixture
                 SKIP_RETURN_CODE
                 77
                 ENVIRONMENT
                 "DISPLAY=;WAYLAND_DISPLAY="
                 TIMEOUT
                 180)
    add_test(NAME benchmark.fixture.gpu.smoke COMMAND $<TARGET_FILE:vkgsplat_fixture_gpu_smoke> ${benchmark_scene}
                                                      ${CMAKE_CURRENT_BINARY_DIR}/benchmark-baseline-v1.png)
    set_tests_properties(
      benchmark.fixture.gpu.smoke
      PROPERTIES FIXTURES_REQUIRED
                 benchmark_fixture
                 SKIP_RETURN_CODE
                 77
                 ENVIRONMENT
                 "DISPLAY=;WAYLAND_DISPLAY="
                 TIMEOUT
                 180)
  endif()
endif()

# CMake-script regression tests for cmake/Sanitizers.cmake (issue #141).
add_test(NAME cmake.sanitizers.apple_excludes_leak
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_sanitizers_apple_excludes_leak.cmake)
add_test(NAME cmake.sanitizers.non_apple_includes_leak
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_sanitizers_non_apple_includes_leak.cmake)
