include(cmake/LibFuzzer.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)


include(CheckCXXSourceCompiles)


macro(vkgsplat_supports_sanitizers)
  # Emscripten doesn't support sanitizers
  if(EMSCRIPTEN)
    set(SUPPORTS_UBSAN OFF)
    set(SUPPORTS_ASAN OFF)
  elseif((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND NOT WIN32)

    message(STATUS "Sanity checking UndefinedBehaviorSanitizer, it should be supported on this platform")
    set(TEST_PROGRAM "int main() { return 0; }")

    # Check if UndefinedBehaviorSanitizer works at link time
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
    check_cxx_source_compiles("${TEST_PROGRAM}" HAS_UBSAN_LINK_SUPPORT)

    if(HAS_UBSAN_LINK_SUPPORT)
      message(STATUS "UndefinedBehaviorSanitizer is supported at both compile and link time.")
      set(SUPPORTS_UBSAN ON)
    else()
      message(WARNING "UndefinedBehaviorSanitizer is NOT supported at link time.")
      set(SUPPORTS_UBSAN OFF)
    endif()
  else()
    set(SUPPORTS_UBSAN OFF)
  endif()

  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND WIN32)
    set(SUPPORTS_ASAN OFF)
  else()
    if (NOT WIN32)
      message(STATUS "Sanity checking AddressSanitizer, it should be supported on this platform")
      set(TEST_PROGRAM "int main() { return 0; }")

      # Check if AddressSanitizer works at link time
      set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
      set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
      check_cxx_source_compiles("${TEST_PROGRAM}" HAS_ASAN_LINK_SUPPORT)

      if(HAS_ASAN_LINK_SUPPORT)
        message(STATUS "AddressSanitizer is supported at both compile and link time.")
        set(SUPPORTS_ASAN ON)
      else()
        message(WARNING "AddressSanitizer is NOT supported at link time.")
        set(SUPPORTS_ASAN OFF)
      endif()
    else()
      set(SUPPORTS_ASAN ON)
    endif()
  endif()
endmacro()

macro(vkgsplat_setup_options)
  option(vkgsplat_ENABLE_HARDENING "Enable hardening" ON)
  option(vkgsplat_ENABLE_COVERAGE "Enable coverage reporting" OFF)
  cmake_dependent_option(
    vkgsplat_ENABLE_GLOBAL_HARDENING
    "Attempt to push hardening options to built dependencies"
    ON
    vkgsplat_ENABLE_HARDENING
    OFF)

  vkgsplat_supports_sanitizers()

  if(NOT PROJECT_IS_TOP_LEVEL OR vkgsplat_PACKAGING_MAINTAINER_MODE)
    option(vkgsplat_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(vkgsplat_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(vkgsplat_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(vkgsplat_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(vkgsplat_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(vkgsplat_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(vkgsplat_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(vkgsplat_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(vkgsplat_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(vkgsplat_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(vkgsplat_ENABLE_PCH "Enable precompiled headers" OFF)
    option(vkgsplat_ENABLE_CACHE "Enable ccache" OFF)
  else()
    option(vkgsplat_ENABLE_IPO "Enable IPO/LTO" ON)
    option(vkgsplat_WARNINGS_AS_ERRORS "Treat Warnings As Errors" ON)
    option(vkgsplat_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${SUPPORTS_ASAN})
    option(vkgsplat_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(vkgsplat_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${SUPPORTS_UBSAN})
    option(vkgsplat_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(vkgsplat_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(vkgsplat_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(vkgsplat_ENABLE_CLANG_TIDY "Enable clang-tidy" ON)
    option(vkgsplat_ENABLE_CPPCHECK "Enable cpp-check analysis" ON)
    option(vkgsplat_ENABLE_PCH "Enable precompiled headers" OFF)
    option(vkgsplat_ENABLE_CACHE "Enable ccache" ON)
  endif()

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      vkgsplat_ENABLE_IPO
      vkgsplat_WARNINGS_AS_ERRORS
      vkgsplat_ENABLE_SANITIZER_ADDRESS
      vkgsplat_ENABLE_SANITIZER_LEAK
      vkgsplat_ENABLE_SANITIZER_UNDEFINED
      vkgsplat_ENABLE_SANITIZER_THREAD
      vkgsplat_ENABLE_SANITIZER_MEMORY
      vkgsplat_ENABLE_UNITY_BUILD
      vkgsplat_ENABLE_CLANG_TIDY
      vkgsplat_ENABLE_CPPCHECK
      vkgsplat_ENABLE_COVERAGE
      vkgsplat_ENABLE_PCH
      vkgsplat_ENABLE_CACHE)
  endif()

  vkgsplat_check_libfuzzer_support(LIBFUZZER_SUPPORTED)
  if(LIBFUZZER_SUPPORTED AND (vkgsplat_ENABLE_SANITIZER_ADDRESS OR vkgsplat_ENABLE_SANITIZER_THREAD OR vkgsplat_ENABLE_SANITIZER_UNDEFINED))
    set(DEFAULT_FUZZER ON)
  else()
    set(DEFAULT_FUZZER OFF)
  endif()

  option(vkgsplat_BUILD_FUZZ_TESTS "Enable fuzz testing executable" ${DEFAULT_FUZZER})

endmacro()

macro(vkgsplat_global_options)
  if(vkgsplat_ENABLE_IPO)
    include(cmake/InterproceduralOptimization.cmake)
    vkgsplat_enable_ipo()
  endif()

  vkgsplat_supports_sanitizers()

  if(vkgsplat_ENABLE_HARDENING AND vkgsplat_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR vkgsplat_ENABLE_SANITIZER_UNDEFINED
       OR vkgsplat_ENABLE_SANITIZER_ADDRESS
       OR vkgsplat_ENABLE_SANITIZER_THREAD
       OR vkgsplat_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    message("${vkgsplat_ENABLE_HARDENING} ${ENABLE_UBSAN_MINIMAL_RUNTIME} ${vkgsplat_ENABLE_SANITIZER_UNDEFINED}")
    vkgsplat_enable_hardening(vkgsplat_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()
endmacro()

macro(vkgsplat_local_options)
  if(PROJECT_IS_TOP_LEVEL)
    include(cmake/StandardProjectSettings.cmake)
  endif()

  add_library(vkgsplat_warnings INTERFACE)
  add_library(vkgsplat_options INTERFACE)

  include(cmake/CompilerWarnings.cmake)
  vkgsplat_set_project_warnings(
    vkgsplat_warnings
    ${vkgsplat_WARNINGS_AS_ERRORS}
    ""
    ""
    ""
    "")

  include(cmake/Linker.cmake)
  # Must configure each target with linker options, we're avoiding setting it globally for now

  if(NOT EMSCRIPTEN)
    include(cmake/Sanitizers.cmake)
    vkgsplat_enable_sanitizers(
      vkgsplat_options
      ${vkgsplat_ENABLE_SANITIZER_ADDRESS}
      ${vkgsplat_ENABLE_SANITIZER_LEAK}
      ${vkgsplat_ENABLE_SANITIZER_UNDEFINED}
      ${vkgsplat_ENABLE_SANITIZER_THREAD}
      ${vkgsplat_ENABLE_SANITIZER_MEMORY})
  endif()

  set_target_properties(vkgsplat_options PROPERTIES UNITY_BUILD ${vkgsplat_ENABLE_UNITY_BUILD})

  if(vkgsplat_ENABLE_PCH)
    target_precompile_headers(
      vkgsplat_options
      INTERFACE
      <vector>
      <string>
      <utility>)
  endif()

  if(vkgsplat_ENABLE_CACHE)
    include(cmake/Cache.cmake)
    vkgsplat_enable_cache()
  endif()

  include(cmake/StaticAnalyzers.cmake)
  if(vkgsplat_ENABLE_CLANG_TIDY)
    vkgsplat_enable_clang_tidy(vkgsplat_options ${vkgsplat_WARNINGS_AS_ERRORS})
  endif()

  if(vkgsplat_ENABLE_CPPCHECK)
    vkgsplat_enable_cppcheck(${vkgsplat_WARNINGS_AS_ERRORS} "" # override cppcheck options
    )
  endif()

  if(vkgsplat_ENABLE_COVERAGE)
    include(cmake/Tests.cmake)
    vkgsplat_enable_coverage(vkgsplat_options)
  endif()

  if(vkgsplat_WARNINGS_AS_ERRORS)
    check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
    if(LINKER_FATAL_WARNINGS)
      # This is not working consistently, so disabling for now
      # target_link_options(vkgsplat_options INTERFACE -Wl,--fatal-warnings)
    endif()
  endif()

  if(vkgsplat_ENABLE_HARDENING AND NOT vkgsplat_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR vkgsplat_ENABLE_SANITIZER_UNDEFINED
       OR vkgsplat_ENABLE_SANITIZER_ADDRESS
       OR vkgsplat_ENABLE_SANITIZER_THREAD
       OR vkgsplat_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    vkgsplat_enable_hardening(vkgsplat_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()

endmacro()
