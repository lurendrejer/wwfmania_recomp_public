# The native side of the Android app: SDL2 (its own CMake project, fetched into android/SDL) and the game, built from
# the repository's CMake project with the recompiled code taken as it is from a host build (WWF_HOST_GEN, the gen
# directory of a desktop `cmake` run of the same source with the same mods): no Python in the Android build.
cmake_minimum_required(VERSION 3.16)
project(wwf_android C)

set(SDL_DIR "" CACHE PATH "SDL2 source (android/fetch-sdl.sh)")
set(WWF_HOST_GEN "" CACHE PATH "gen directory of a host build")
if(NOT EXISTS "${SDL_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "SDL2 source not found in '${SDL_DIR}': run android/fetch-sdl.sh")
endif()
if(NOT EXISTS "${WWF_HOST_GEN}/c/modules.cmake")
  message(FATAL_ERROR "No generated game in '${WWF_HOST_GEN}': build the repository on the desktop first (cmake -S . -B build)")
endif()

add_subdirectory("${SDL_DIR}" SDL2)

set(WWF_GEN_DIR "${WWF_HOST_GEN}" CACHE PATH "" FORCE)
set(WWF_REGEN OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../.." wwf)
