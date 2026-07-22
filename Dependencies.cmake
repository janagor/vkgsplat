include(cmake/CPM.cmake)

# Done as a function so that updates to variables like
# CMAKE_CXX_FLAGS don't propagate out to other
# targets
function(vkgsplat_setup_dependencies)

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

  if(NOT TARGET glfw AND NOT TARGET glfw::glfw)
    cpmaddpackage(
      NAME
      glfw
      GITHUB_REPOSITORY
      "glfw/glfw"
      GIT_TAG
      "3.4"
      SYSTEM
      YES)
  endif()

  if(TARGET glfw AND NOT TARGET glfw::glfw)
    add_library(glfw::glfw ALIAS glfw)
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

  if(NOT TARGET tools::tools)
    cpmaddpackage(
      NAME
      tools
      GITHUB_REPOSITORY
      "lefticus/tools"
      GIT_TAG
      "main")
  endif()

  # Dear ImGui from achaulk/imgui@desc_heap (Vulkan descriptor-heap backend).
  # Upstream ImGui has no CMakeLists.txt, so fetch sources and build a target.
  if(NOT TARGET imgui::imgui)
    cpmaddpackage(
      NAME
      imgui
      GITHUB_REPOSITORY
      "achaulk/imgui"
      GIT_TAG
      "689e4dc5d5ea6238034836015ff33a0d3ac8bcdf"
      DOWNLOAD_ONLY
      YES
      SYSTEM
      YES
      PATCHES
      "${CMAKE_SOURCE_DIR}/cmake/patches/imgui-desc-heap-dynamic-rendering.patch")

    add_library(
      imgui
      STATIC
      ${imgui_SOURCE_DIR}/imgui.cpp
      ${imgui_SOURCE_DIR}/imgui_demo.cpp
      ${imgui_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
      ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

    target_include_directories(
      imgui
      SYSTEM
      PUBLIC
      ${imgui_SOURCE_DIR}
      ${imgui_SOURCE_DIR}/backends)

    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

    target_link_libraries(imgui PUBLIC glfw::glfw Vulkan::Headers Vulkan::Vulkan)

    add_library(imgui::imgui ALIAS imgui)
  endif()

endfunction()
