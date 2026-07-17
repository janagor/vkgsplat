include_guard(GLOBAL)

find_program(VKGSPLAT_GLSLC glslc)
find_program(VKGSPLAT_GLSLANG_VALIDATOR glslangValidator)

if(VKGSPLAT_GLSLC)
  set(VKGSPLAT_SHADER_COMPILER "${VKGSPLAT_GLSLC}")
  set(VKGSPLAT_SHADER_COMPILER_TYPE "glslc")
elseif(VKGSPLAT_GLSLANG_VALIDATOR)
  set(VKGSPLAT_SHADER_COMPILER "${VKGSPLAT_GLSLANG_VALIDATOR}")
  set(VKGSPLAT_SHADER_COMPILER_TYPE "glslang")
else()
  message(FATAL_ERROR "No SPIR-V shader compiler found. Install glslc or glslangValidator.")
endif()

# Compile GLSL/HLSL sources to SPIR-V and attach them as build dependencies on TARGET.
#
# Usage:
#   vkgsplat_compile_shaders(
#     TARGET triangle
#     OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders"
#     SOURCES shaders/triangle.vert shaders/triangle.frag ...
#   )
function(vkgsplat_compile_shaders)
  set(options)
  set(one_value_args TARGET OUTPUT_DIR)
  set(multi_value_args SOURCES)
  cmake_parse_arguments(VKGSPLAT "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT VKGSPLAT_TARGET)
    message(FATAL_ERROR "vkgsplat_compile_shaders: TARGET is required")
  endif()

  if(NOT VKGSPLAT_OUTPUT_DIR)
    set(VKGSPLAT_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()

  if(NOT VKGSPLAT_SOURCES)
    message(FATAL_ERROR "vkgsplat_compile_shaders: SOURCES is required")
  endif()

  file(MAKE_DIRECTORY "${VKGSPLAT_OUTPUT_DIR}")

  set(_spirv_outputs)
  foreach(_shader ${VKGSPLAT_SOURCES})
    if(NOT IS_ABSOLUTE "${_shader}")
      set(_shader "${CMAKE_SOURCE_DIR}/${_shader}")
    endif()

    get_filename_component(_shader_name "${_shader}" NAME)
    set(_spirv "${VKGSPLAT_OUTPUT_DIR}/${_shader_name}.spv")

    if(VKGSPLAT_SHADER_COMPILER_TYPE STREQUAL "glslc")
      add_custom_command(
        OUTPUT "${_spirv}"
        COMMAND "${VKGSPLAT_SHADER_COMPILER}" --target-env=vulkan1.4 "${_shader}" -o "${_spirv}"
        DEPENDS "${_shader}"
        COMMENT "Compiling shader ${_shader_name}"
        VERBATIM)
    else()
      add_custom_command(
        OUTPUT "${_spirv}"
        COMMAND "${VKGSPLAT_SHADER_COMPILER}" -V --target-env vulkan1.4 "${_shader}" -o "${_spirv}"
        DEPENDS "${_shader}"
        COMMENT "Compiling shader ${_shader_name}"
        VERBATIM)
    endif()

    list(APPEND _spirv_outputs "${_spirv}")
  endforeach()

  set(_shader_target "${VKGSPLAT_TARGET}_shaders")
  add_custom_target("${_shader_target}" DEPENDS ${_spirv_outputs})
  add_dependencies("${VKGSPLAT_TARGET}" "${_shader_target}")
endfunction()
