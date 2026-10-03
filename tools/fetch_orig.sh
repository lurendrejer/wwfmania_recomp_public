#!/bin/sh
# Downloads the original game (its assembly source and art) from historicalsource/wwf-wrestlemania into orig/.
# orig/ is not part of this repository; the port needs it to build and run (orig/README.md).
set -e
cd "$(dirname "$0")/.."
if [ -f orig/WRESTLE.CMD ]; then
    echo "orig/ is already present"
    exit 0
fi
url=${ORIG_URL:-https://github.com/historicalsource/wwf-wrestlemania/archive/refs/heads/main.zip}
tmp=$(mktemp -d)
echo "Downloading $url"
if ! curl -fL -o "$tmp/orig.zip" "$url"; then
    echo "download failed; alternatively: git clone the project and copy its files into orig/" >&2
    rm -rf "$tmp"
    exit 1
fi
unzip -q "$tmp/orig.zip" -d "$tmp"
rm -f "$tmp/orig.zip"
rm -rf orig
mv "$tmp"/* orig
rm -rf "$tmp"
[ -f orig/WRESTLE.CMD ] || { echo "unexpected archive layout" >&2; exit 1; }
echo "orig/ ready"
