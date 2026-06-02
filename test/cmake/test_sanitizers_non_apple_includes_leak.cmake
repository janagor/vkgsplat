# Companion to test_sanitizers_apple_excludes_leak.cmake (#141).
# Verifies that on non-Apple platforms the leak sanitizer is still
# propagated to the compiler/linker, so the Apple-specific guard does
# not silently disable leak detection elsewhere.

cmake_minimum_required(VERSION 3.21)

set(APPLE FALSE)
set(MSVC FALSE)
set(CMAKE_CXX_COMPILER_ID "Clang")

set_property(GLOBAL PROPERTY captured_compile_options "")
set_property(GLOBAL PROPERTY captured_link_options "")

function(target_compile_options)
  set_property(GLOBAL PROPERTY captured_compile_options "${ARGN}")
endfunction()

function(target_link_options)
  set_property(GLOBAL PROPERTY captured_link_options "${ARGN}")
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Sanitizers.cmake")

myproject_enable_sanitizers(
  test_target
  OFF # ENABLE_SANITIZER_ADDRESS
  ON  # ENABLE_SANITIZER_LEAK
  OFF # ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
  OFF # ENABLE_SANITIZER_THREAD
  OFF # ENABLE_SANITIZER_MEMORY
)

get_property(compile_options GLOBAL PROPERTY captured_compile_options)
get_property(link_options GLOBAL PROPERTY captured_link_options)

message(STATUS "Captured compile options: ${compile_options}")
message(STATUS "Captured link options:    ${link_options}")

if(NOT compile_options MATCHES "leak")
  message(
    FATAL_ERROR
    "Leak sanitizer must still be enabled on non-Apple platforms, "
    "but compile options were: ${compile_options}")
endif()

if(NOT link_options MATCHES "leak")
  message(
    FATAL_ERROR
    "Leak sanitizer must still be enabled on non-Apple platforms, "
    "but link options were: ${link_options}")
endif()
