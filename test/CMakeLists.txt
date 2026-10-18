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
    target_link_libraries(vkgsplat_headless_platform_test PRIVATE
      Catch2::Catch2WithMain vkgsplat::vkgsplat_headless_platform)
    catch_discover_tests(vkgsplat_headless_platform_test)
  endif()

  if(TARGET vkgsplat::vkgsplat_backend)
    add_executable(vkgsplat_vulkan_surface_test vulkan_surface.cpp)
    target_link_libraries(vkgsplat_vulkan_surface_test PRIVATE
      Catch2::Catch2WithMain vkgsplat::vkgsplat_backend)
    catch_discover_tests(vkgsplat_vulkan_surface_test)
  endif()
endif()

option(vkgsplat_ENABLE_HEADLESS_GPU_TESTS "Enable GPU-dependent headless integration tests" OFF)
option(vkgsplat_HEADLESS_GPU_TEST_VALIDATION "Enable validation layers in the headless GPU test" ON)
set(vkgsplat_HEADLESS_TEST_PLY "" CACHE FILEPATH "PLY fixture for headless GPU integration test")
if(TARGET vkgsplat_headless)
  add_test(NAME headless.cli.arguments COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:vkgsplat_headless>
    -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_cli.cmake)
endif()
if(vkgsplat_ENABLE_HEADLESS_GPU_TESTS)
  if(NOT TARGET vkgsplat_headless OR NOT EXISTS "${vkgsplat_HEADLESS_TEST_PLY}")
    message(FATAL_ERROR "Headless GPU tests require a native build and vkgsplat_HEADLESS_TEST_PLY")
  endif()
  add_test(NAME headless.gpu.smoke COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:vkgsplat_headless>
    -DPLY=${vkgsplat_HEADLESS_TEST_PLY}
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/headless-test.png
    -DVALIDATION=${vkgsplat_HEADLESS_GPU_TEST_VALIDATION}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_gpu_smoke.cmake)
  set_tests_properties(headless.gpu.smoke PROPERTIES ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=" TIMEOUT 120)
endif()

# CMake-script regression tests for cmake/Sanitizers.cmake (issue #141).
add_test(NAME cmake.sanitizers.apple_excludes_leak
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_sanitizers_apple_excludes_leak.cmake)
add_test(NAME cmake.sanitizers.non_apple_includes_leak
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_sanitizers_non_apple_includes_leak.cmake)
