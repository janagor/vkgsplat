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

# CMake-script regression tests for cmake/Sanitizers.cmake (issue #141).
add_test(NAME cmake.sanitizers.apple_excludes_leak
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_sanitizers_apple_excludes_leak.cmake)
add_test(NAME cmake.sanitizers.non_apple_includes_leak
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_sanitizers_non_apple_includes_leak.cmake)
