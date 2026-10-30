include(cmake/CPM.cmake)

# Done as a function so that updates to variables like
# CMAKE_CXX_FLAGS don't propagate out to other
# targets
function(vkgsplat_setup_dependencies)

  if(vkgsplat_BUILD_BENCHMARKS
     AND NOT EMSCRIPTEN
     AND NOT TARGET benchmark::benchmark)
    cpmaddpackage(
      NAME
      benchmark
      GITHUB_REPOSITORY
      google/benchmark
      GIT_TAG
      v1.10.0
      SYSTEM
      YES
      OPTIONS
      "BENCHMARK_ENABLE_TESTING OFF"
      "BENCHMARK_ENABLE_GTEST_TESTS OFF"
      "BENCHMARK_ENABLE_INSTALL OFF")
  endif()

  # For each dependency, see if it's
  # already been provided to us by a parent project

  if(NOT TARGET Vulkan::Vulkan)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
      find_package(PkgConfig REQUIRED)
      pkg_check_modules(
        Vulkan
        REQUIRED
        IMPORTED_TARGET
        vulkan)
      add_library(Vulkan::Vulkan ALIAS PkgConfig::Vulkan)
    endif()
  endif()

  if(NOT TARGET Vulkan::Headers)
    cpmaddpackage(
      NAME
      Vulkan-Headers
      GITHUB_REPOSITORY
      KhronosGroup/Vulkan-Headers
      GIT_TAG
      "v1.4.352"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET vk-bootstrap::vk-bootstrap)
    cpmaddpackage(
      NAME
      vk-bootstrap
      GITHUB_REPOSITORY
      "charles-lunarg/vk-bootstrap"
      GIT_TAG
      "v1.4.352"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
    cpmaddpackage(
      NAME
      VulkanMemoryAllocator
      GITHUB_REPOSITORY
      "GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator"
      GIT_TAG
      "v3.4.0"
      SYSTEM
      YES)
  endif()

  # vkexec Vulkan *exec backend (pinned GitHub). Fetched after Vulkan/VMA/vk-bootstrap
  # so nested Dependencies.cmake reuses those targets (no second hard find_package).
  set(vkexec_BUILD_EXAMPLES
      OFF
      CACHE BOOL "Build vkexec example executables" FORCE)
  set(vkexec_BUILD_VMA
      ON
      CACHE BOOL "Build vkexec VMA resource module" FORCE)
  if(NOT TARGET vkexec::vkexec)
    cpmaddpackage(
      NAME
      vkexec
      GITHUB_REPOSITORY
      "janagor/vkexec"
      GIT_TAG
      "fb6b1f71d719b8943ccde48d237103bb3b305d75"
      SYSTEM
      YES
      OPTIONS
      "vkexec_BUILD_EXAMPLES OFF")
  endif()
  # The selected vkexec revision has three factory lookups that resolve to
  # vkexec::factory::detail instead of the declared vkexec::detail namespace.
  # Keep this narrow compatibility patch until the dependency fixes those headers.
  set(_vkexec_factory_patch "${PROJECT_SOURCE_DIR}/cmake/patches/vkexec-factory-detail-namespace.patch")
  if(DEFINED vkexec_SOURCE_DIR AND EXISTS "${vkexec_SOURCE_DIR}/include/vkexec/compute_pipeline.hpp")
    file(READ "${vkexec_SOURCE_DIR}/include/vkexec/compute_pipeline.hpp" _vkexec_compute_pipeline_header)
    file(READ "${vkexec_SOURCE_DIR}/include/vkexec_extensions/timeline_semaphore/frame_ring.hpp"
         _vkexec_frame_ring_header)
    string(FIND "${_vkexec_compute_pipeline_header}" "make_sender(detail::make_compute_pipeline_spirv_factory"
                _vkexec_bad_spirv)
    string(FIND "${_vkexec_compute_pipeline_header}" "make_sender(detail::make_compute_pipeline_glsl_factory"
                _vkexec_bad_glsl)
    string(FIND "${_vkexec_frame_ring_header}" "make_sender(detail::make_frame_ring_factory" _vkexec_bad_frame_ring)
    string(FIND "${_vkexec_compute_pipeline_header}" "::vkexec::detail::make_compute_pipeline_spirv_factory"
                _vkexec_fixed_spirv)
    string(FIND "${_vkexec_compute_pipeline_header}" "::vkexec::detail::make_compute_pipeline_glsl_factory"
                _vkexec_fixed_glsl)
    string(FIND "${_vkexec_frame_ring_header}" "::vkexec::detail::make_frame_ring_factory" _vkexec_fixed_frame_ring)
    if(NOT
       _vkexec_bad_spirv
       EQUAL
       -1
       AND NOT
           _vkexec_bad_glsl
           EQUAL
           -1
       AND NOT
           _vkexec_bad_frame_ring
           EQUAL
           -1
       AND _vkexec_fixed_spirv EQUAL -1
       AND _vkexec_fixed_glsl EQUAL -1
       AND _vkexec_fixed_frame_ring EQUAL -1)
      find_package(Git REQUIRED)
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${_vkexec_factory_patch}"
        WORKING_DIRECTORY "${vkexec_SOURCE_DIR}"
        RESULT_VARIABLE _vkexec_patch_result)
      if(NOT
         _vkexec_patch_result
         EQUAL
         0)
        message(FATAL_ERROR "Failed to apply vkexec factory namespace compatibility patch")
      endif()
    elseif(
      _vkexec_fixed_spirv EQUAL -1
      OR _vkexec_fixed_glsl EQUAL -1
      OR _vkexec_fixed_frame_ring EQUAL -1)
      message(FATAL_ERROR "Pinned vkexec factory headers differ from the expected compatibility patch")
    endif()
  endif()
  # Nested vkexec includes <vulkan/...> but does not link Vulkan::Headers; attach the
  # parent CPM Vulkan-Headers target so the build does not rely on system headers.
  if(TARGET vkexec AND TARGET Vulkan::Headers)
    target_link_libraries(vkexec PUBLIC Vulkan::Headers)
  endif()
  if(TARGET vkexec_vma AND TARGET Vulkan::Headers)
    target_link_libraries(vkexec_vma PUBLIC Vulkan::Headers)
  endif()

  if(NOT TARGET glfw AND NOT TARGET glfw::glfw)
    cpmaddpackage(
      NAME
      glfw
      GITHUB_REPOSITORY
      "glfw/glfw"
      GIT_TAG
      "3.4"
      SYSTEM
      YES
      OPTIONS
      "GLFW_BUILD_DOCS OFF"
      "GLFW_BUILD_TESTS OFF"
      "GLFW_BUILD_EXAMPLES OFF")
  endif()

  if(TARGET glfw AND NOT TARGET glfw::glfw)
    add_library(glfw::glfw ALIAS glfw)
  endif()

  # Vulkan-only consumers: glfw3.h must not pull in system OpenGL headers.
  if(TARGET glfw)
    target_compile_definitions(glfw INTERFACE GLFW_INCLUDE_NONE)
  endif()

  if(NOT TARGET glm::glm)
    cpmaddpackage(
      NAME
      glm
      GITHUB_REPOSITORY
      "g-truc/glm"
      GIT_TAG
      "1.0.3"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET magic_enum::magic_enum)
    cpmaddpackage(
      NAME
      magic_enum
      GITHUB_REPOSITORY
      "Neargye/magic_enum"
      GIT_TAG
      "v0.9.8"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET beman::indirect)
    cpmaddpackage(
      NAME
      indirect
      GITHUB_REPOSITORY
      "bemanproject/indirect"
      GIT_TAG
      "v0.1.0"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET fmtlib::fmtlib)
    cpmaddpackage(
      NAME
      fmt
      GITHUB_REPOSITORY
      "fmtlib/fmt"
      GIT_TAG
      "12.1.0"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET spdlog::spdlog)
    cpmaddpackage(
      NAME
      spdlog
      VERSION
      1.17.0
      GITHUB_REPOSITORY
      "gabime/spdlog"
      SYSTEM
      YES
      OPTIONS
      "SPDLOG_FMT_EXTERNAL ON")
  endif()

  if(NOT TARGET Catch2::Catch2WithMain)
    cpmaddpackage(
      NAME
      Catch2
      VERSION
      3.12.0
      GITHUB_REPOSITORY
      "catchorg/Catch2"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET CLI11::CLI11)
    cpmaddpackage(
      NAME
      CLI11
      VERSION
      2.6.1
      GITHUB_REPOSITORY
      "CLIUtils/CLI11"
      SYSTEM
      YES)
  endif()

  if(NOT EMSCRIPTEN AND NOT TARGET nlohmann_json::nlohmann_json)
    cpmaddpackage(
      NAME
      nlohmann_json
      GITHUB_REPOSITORY
      "nlohmann/json"
      GIT_TAG
      "v3.12.0"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET tools::tools)
    cpmaddpackage(
      NAME
      tools
      GITHUB_REPOSITORY
      "lefticus/tools"
      GIT_TAG
      "main")
  endif()

  if(NOT TARGET stb::stb)
    cpmaddpackage(
      NAME
      stb
      GITHUB_REPOSITORY
      "nothings/stb"
      GIT_TAG
      "2c980bb59875b0d32144a71867fbdebb2f77cd20"
      DOWNLOAD_ONLY
      YES
      SYSTEM
      YES)

    add_library(stb INTERFACE)
    add_library(stb::stb ALIAS stb)
    target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
  endif()

  # Dear ImGui (janagor fork with VK_EXT_descriptor_heap).
  # Upstream ImGui has no CMakeLists.txt, so fetch sources and build a target.
  if(NOT TARGET imgui::imgui)
    cpmaddpackage(
      NAME
      imgui
      GITHUB_REPOSITORY
      "janagor/imgui"
      GIT_TAG
      "VK_EXT_descriptor_heap"
      DOWNLOAD_ONLY
      YES
      SYSTEM
      YES)

    add_library(
      imgui STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_demo.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

    target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

    target_link_libraries(imgui PUBLIC glfw::glfw Vulkan::Headers Vulkan::Vulkan)

    add_library(imgui::imgui ALIAS imgui)
  endif()

  # Reference shaders from MircoWerner/VkRadixSort. Not a linkable library — example +
  # engine code — so we DOWNLOAD_ONLY for the upstream pin and compile our heap/KV
  # overlays (same algorithm, adapted bindings).
  cpmaddpackage(
    NAME
    VkRadixSort
    GITHUB_REPOSITORY
    MircoWerner/VkRadixSort
    GIT_TAG
    "029c351d24d9a6c6d680de272cd24d4448a34373"
    DOWNLOAD_ONLY
    YES)

  set(_vkradixsort_overlay "${PROJECT_SOURCE_DIR}/cmake/overlays/vkradixsort")
  foreach(_shader multi_radixsort_histograms.comp multi_radixsort.comp)
    if(NOT EXISTS "${_vkradixsort_overlay}/${_shader}")
      message(FATAL_ERROR "Missing VkRadixSort overlay: ${_vkradixsort_overlay}/${_shader}")
    endif()
  endforeach()

  set(VKGSPLAT_VKRADIXSORT_SHADERS_DIR
      "${_vkradixsort_overlay}"
      CACHE PATH "VkRadixSort multi-radix overlay shader directory" FORCE)
  message(STATUS "VkRadixSort shaders (overlay): ${VKGSPLAT_VKRADIXSORT_SHADERS_DIR}")
  message(STATUS "VkRadixSort upstream source: ${VkRadixSort_SOURCE_DIR}")

endfunction()
