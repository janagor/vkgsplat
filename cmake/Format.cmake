function(vkgsplat_enable_formatting)
  if(NOT PROJECT_IS_TOP_LEVEL)
    return()
  endif()

  set(CMAKE_FORMAT_EXTRA_ARGS
      "-c ${CMAKE_SOURCE_DIR}/.cmake-format.yaml"
      CACHE STRING "Extra arguments passed to cmake-format" FORCE)
  set(CMAKE_FORMAT_EXCLUDE
      "cmake/CPM\\.cmake"
      CACHE STRING "Regex of cmake files excluded from cmake-format" FORCE)

  cpmaddpackage(
    NAME
    Format.cmake
    VERSION
    1.8.2
    GITHUB_REPOSITORY
    TheLartians/Format.cmake
    OPTIONS
    "FORMAT_SKIP_CMAKE NO"
    "FORMAT_SKIP_CLANG NO")
endfunction()
