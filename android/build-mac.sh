#!/bin/sh
# Builds the Android APK on macOS: generates the game (Python), fetches SDL2 if needed, runs Gradle.
# The images (orig/IMG), the generated data, art/ and sounds/ are put into the APK if they are there.
#
#   sh android/build-mac.sh              build
#   sh android/build-mac.sh --install    build and install on the connected device (adb)
#   sh android/build-mac.sh --nodata     small APK without images, sounds and art: push them with adb (docs/ANDROID.md)
#   sh android/build-mac.sh --regen      generate the game again first (after changing tools/gsp, mods, src/wolf)
set -e
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)

INSTALL=0
REGEN=0
GRADLE_ARGS=
for a in "$@"; do
    [ "$a" = "--nodata" ] && GRADLE_ARGS="-PwwfNoData=1"
    [ "$a" = "--install" ] && INSTALL=1
    [ "$a" = "--regen" ] && REGEN=1
done

if [ -z "$JAVA_HOME" ] && command -v brew >/dev/null 2>&1 && [ -d "$(brew --prefix openjdk@17 2>/dev/null)" ]; then
    JAVA_HOME=$(brew --prefix openjdk@17)
    export JAVA_HOME
fi
if [ ! -f local.properties ] && [ -z "$ANDROID_HOME" ]; then
    echo "No Android SDK is set up: run  sh android/setup-mac.sh  first"
    exit 1
fi

# the generated code goes stale when the mods, the translator or the machine change
STAMP="$ROOT/build/gen/c/modules.cmake"
if [ -f "$STAMP" ] && [ -n "$(find "$ROOT/mods" "$ROOT/tools/gsp" "$ROOT/orig" -type f \( -name '*.ASM' -o -name '*.py' -o -name 'gen.txt' \) -newer "$STAMP" 2>/dev/null | head -1)" ]; then
    echo "The mods or the translator changed: generating the game again"
    REGEN=1
fi
if [ "$REGEN" = 1 ] || [ ! -f "$STAMP" ]; then
    python3 regen_gen.py "$ROOT/build/gen"
fi
[ -d SDL/android-project ] || sh fetch-sdl.sh

./gradlew assembleRelease $GRADLE_ARGS

APK=app/build/outputs/apk/release/app-release.apk
echo
echo "APK: $(pwd)/$APK"
ls -lh "$APK"
if [ "$INSTALL" = 1 ]; then
    ADB=adb
    command -v adb >/dev/null 2>&1 || ADB=$(sed -n 's/^sdk.dir=//p' local.properties)/platform-tools/adb
    "$ADB" install -r "$APK"
fi
