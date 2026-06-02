# Regression test for #141: macOS ARM doesn't support leak sanitizer.
#
# Simulates an Apple toolchain by setting APPLE=TRUE before including
# Sanitizers.cmake, stubs the target_*_options commands so that the flags
# requested by myproject_enable_sanitizers can be inspected, then asserts
# that "leak" is not propagated to the compiler/linker even when the user
# enables the leak sanitizer option.

cmake_minimum_required(VERSION 3.21)

set(APPLE TRUE)
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
  ON  # ENABLE_SANITIZER_ADDRESS
  ON  # ENABLE_SANITIZER_LEAK (should be filtered out on Apple)
  OFF # ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
  OFF # ENABLE_SANITIZER_THREAD
  OFF # ENABLE_SANITIZER_MEMORY
)

get_property(compile_options GLOBAL PROPERTY captured_compile_options)
get_property(link_options GLOBAL PROPERTY captured_link_options)

message(STATUS "Captured compile options: ${compile_options}")
message(STATUS "Captured link options:    ${link_options}")

if(compile_options MATCHES "leak")
  message(
    FATAL_ERROR
    "Leak sanitizer must not be enabled on Apple platforms (#141), "
    "but compile options contain 'leak': ${compile_options}")
endif()

if(link_options MATCHES "leak")
  message(
    FATAL_ERROR
    "Leak sanitizer must not be enabled on Apple platforms (#141), "
    "but link options contain 'leak': ${link_options}")
endif()

if(NOT compile_options MATCHES "address")
  message(
    FATAL_ERROR
    "Address sanitizer should still be enabled when leak is filtered out, "
    "but compile options were: ${compile_options}")
endif()
