#!/bin/sh
# One-time set-up on macOS for building the Android app: Java 17, the Android command line tools, and the SDK/NDK
# packages the build asks for. Safe to run again. Uses Homebrew. Written without a Mac to run it on: tell me what fails.
#
#   sh android/setup-mac.sh
set -e
cd "$(dirname "$0")"

if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is needed first: https://brew.sh"
    exit 1
fi

echo "== Java 17"
brew list openjdk@17 >/dev/null 2>&1 || brew install openjdk@17
JAVA_HOME=$(brew --prefix openjdk@17)

echo "== Android command line tools"
brew list --cask android-commandlinetools >/dev/null 2>&1 || brew install --cask android-commandlinetools
SDK=$(brew --prefix)/share/android-commandlinetools
SDKM=$(find "$SDK" -name sdkmanager -type f 2>/dev/null | head -n 1)
[ -z "$SDKM" ] && SDKM=$(command -v sdkmanager || true)
if [ -z "$SDKM" ]; then
    echo "sdkmanager was not found under $SDK"
    exit 1
fi

echo "== SDK and NDK packages (a big download the first time)"
export JAVA_HOME
yes | "$SDKM" --sdk_root="$SDK" --licenses >/dev/null || true
"$SDKM" --sdk_root="$SDK" "platforms;android-34" "build-tools;34.0.0" "platform-tools" \
    "ndk;26.1.10909125" "cmake;3.22.1"

echo "sdk.dir=$SDK" > local.properties
echo "== done: android/local.properties points at $SDK"
echo "Next: sh android/build-mac.sh"
