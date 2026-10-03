#!/bin/sh
# Fetches the SDL2 source (its CMake project and the Java side of SDL on Android) into android/SDL.
# Untested: written without an Android SDK to run it against.
set -e
VER=${SDL_VERSION:-2.30.9}
cd "$(dirname "$0")"
if [ -d SDL/android-project ]; then
    echo "SDL already in android/SDL"
    exit 0
fi
curl -fL -o /tmp/SDL2-$VER.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$VER/SDL2-$VER.tar.gz
mkdir -p SDL
tar -xzf /tmp/SDL2-$VER.tar.gz -C SDL --strip-components=1
echo "SDL $VER in android/SDL"
