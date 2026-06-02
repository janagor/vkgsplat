macro(vkgsplat_configure_linker project_name)
  set(vkgsplat_USER_LINKER_OPTION
    "DEFAULT"
      CACHE STRING "Linker to be used")
    set(vkgsplat_USER_LINKER_OPTION_VALUES "DEFAULT" "SYSTEM" "LLD" "GOLD" "BFD" "MOLD" "SOLD" "APPLE_CLASSIC" "MSVC")
  set_property(CACHE vkgsplat_USER_LINKER_OPTION PROPERTY STRINGS ${vkgsplat_USER_LINKER_OPTION_VALUES})
  list(
    FIND
    vkgsplat_USER_LINKER_OPTION_VALUES
    ${vkgsplat_USER_LINKER_OPTION}
    vkgsplat_USER_LINKER_OPTION_INDEX)

  if(${vkgsplat_USER_LINKER_OPTION_INDEX} EQUAL -1)
    message(
      STATUS
        "Using custom linker: '${vkgsplat_USER_LINKER_OPTION}', explicitly supported entries are ${vkgsplat_USER_LINKER_OPTION_VALUES}")
  endif()

  set_target_properties(${project_name} PROPERTIES LINKER_TYPE "${vkgsplat_USER_LINKER_OPTION}")
endmacro()
