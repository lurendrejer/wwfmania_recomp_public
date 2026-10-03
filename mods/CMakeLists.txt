# Mods (docs/MODS.md). Every subdirectory with a CMakeLists.txt is one mod
# and calls wwf_add_mod(<name> <sources>). Turn the whole layer off with
# -DWWF_MODS=OFF; a mod that is built is still inactive until it is enabled
# at run time (wwf --mod <name>).
set_property(GLOBAL PROPERTY WWF_MOD_NAMES "")

function(wwf_add_mod name)
  add_library(wwf_mod_${name} STATIC ${ARGN})
  target_include_directories(wwf_mod_${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(wwf_mod_${name} PUBLIC wwf_assets wwf_video wwf_sound)
  set_property(GLOBAL APPEND PROPERTY WWF_MOD_NAMES ${name})
endfunction()

file(GLOB _mod_dirs CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*/CMakeLists.txt)
foreach(_f ${_mod_dirs})
  get_filename_component(_d ${_f} DIRECTORY)
  add_subdirectory(${_d})
endforeach()
