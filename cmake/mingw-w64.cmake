# Cross-compiling for Windows (x86_64) with MinGW-w64, e.g. from macOS or Linux.
# Used by tools/build_windows.sh; SDL2_ROOT is the x86_64-w64-mingw32 folder of
# the SDL2 "mingw" development package.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
if(DEFINED ENV{SDL2_ROOT})
  set(CMAKE_FIND_ROOT_PATH $ENV{SDL2_ROOT})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
