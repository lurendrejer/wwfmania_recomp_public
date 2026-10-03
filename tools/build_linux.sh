#!/bin/sh
# Builds the desktop version on Linux and collects what it needs to run into dist-linux/.
#
#   Debian/Ubuntu:  sudo apt install build-essential cmake python3 libsdl2-dev
#   Fedora:         sudo dnf install gcc cmake python3 SDL2-devel
#
#   tools/build_linux.sh            build
#   tools/build_linux.sh test       build and run the tests
#
# Needs orig/ (see README). Sounds are not included: extract them with dcsrip and put the
# sounds/ folder next to the `wwf` program (docs/SOUND.md).
set -e
cd "$(dirname "$0")/.."
for tool in cmake python3 cc; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing: $tool (see the top of this script)"; exit 1; }
done
if [ ! -d orig/IMG ]; then
    echo "orig/IMG is missing: see the README (tools/fetch_orig.sh)"
    exit 1
fi
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$JOBS"
if [ "$1" = "test" ]; then
    ctest --test-dir build --output-on-failure
fi

DIST=dist-linux
rm -rf "$DIST"
mkdir -p "$DIST/orig"
cp build/wwf "$DIST/"
cp -R build/gen "$DIST/gen"
rm -rf "$DIST/gen/c"
cp -R orig/IMG "$DIST/orig/IMG"
cat > "$DIST/wwf.sh" <<'EOF'
#!/bin/sh
# Change the options to taste: --res 1920x1080 --zoom 1 --scale 4 --classic
cd "$(dirname "$0")"
exec ./wwf --gen gen --img orig/IMG "$@"
EOF
chmod +x "$DIST/wwf.sh"
echo "Done: $DIST/ (run ./wwf.sh; put sounds/ next to it for sound)"
