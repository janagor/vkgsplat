# Header-only ADT utilities (e.g. BoundedQueue).
add_library(vkgsplat_adt INTERFACE)
add_library(vkgsplat::vkgsplat_adt ALIAS vkgsplat_adt)

target_link_libraries(vkgsplat_adt INTERFACE vkgsplat::vkgsplat_options)

target_compile_features(vkgsplat_adt INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

target_include_directories(vkgsplat_adt ${WARNING_GUARD} INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                                   $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
