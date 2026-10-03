#!/bin/sh
# Builds the Windows version (wwf.exe) with MinGW-w64, e.g. on a Mac, and
# collects what it needs to run into dist-windows/.
#
#   macOS:  brew install mingw-w64 cmake      (python3 comes with the system)
#   Linux:  apt install gcc-mingw-w64-x86-64 cmake python3
#
#   tools/build_windows.sh
#
# Needs orig/ (see README) and downloads the SDL2 MinGW package once.
# Sounds are not included: extract them with dcsrip and put the sounds/ folder
# next to wwf.exe.
set -e
cd "$(dirname "$0")/.."
SDL_VER=2.30.9
WORK=build-windows
mkdir -p "$WORK"
if [ ! -d "$WORK/SDL2-$SDL_VER" ]; then
    curl -fsSL -o "$WORK/sdl2.tgz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-devel-$SDL_VER-mingw.tar.gz"
    tar xzf "$WORK/sdl2.tgz" -C "$WORK"
fi
SDL2_ROOT="$PWD/$WORK/SDL2-$SDL_VER/x86_64-w64-mingw32"
export SDL2_ROOT
cmake -S . -B "$WORK/out" -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
    -DCMAKE_BUILD_TYPE=Release -DSDL2_DIR="$SDL2_ROOT/lib/cmake/SDL2"
cmake --build "$WORK/out" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" --target wwf

DIST=dist-windows
rm -rf "$DIST"
mkdir -p "$DIST/orig"
cp "$WORK/out/wwf.exe" "$SDL2_ROOT/bin/SDL2.dll" "$DIST/"
cp -R "$WORK/out/gen" "$DIST/gen"
cp -R orig/IMG "$DIST/orig/IMG"
cat > "$DIST/wwf.bat" <<'EOF'
@echo off
rem Change the options to taste: --res 1920x1080 --zoom 1 --scale 4 --classic
wwf.exe --gen gen --img orig\IMG --res 1920x1080 %*
EOF
echo "Done: $DIST/ (run wwf.bat on Windows; put sounds/ next to it for sound)"
