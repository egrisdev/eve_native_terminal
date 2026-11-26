#!/usr/bin/env bash
# Builds EVE Windows Terminal on Linux:
#   1. runs the core unit tests natively
#   2. cross-compiles evewt.exe and evewt-console.exe with mingw-w64
#   3. packages them into dist/EVE-Windows-Terminal-<version>.msi with wixl
# Needs: sudo apt install g++ g++-mingw-w64-x86-64 wixl
#
# Usage: ./build.sh [version]
# The release workflow passes the version from the tag (v1.2.0 -> 1.2.0).
set -euo pipefail
cd "$(dirname "$0")"

VERSION="${1:-1.1.2}"   # the default is the current release; a new MSI needs a higher version to upgrade the installed one
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "build.sh: the version must look like 1.2.3, not '$VERSION'" >&2
    exit 1
fi
IFS=. read -r MAJOR MINOR PATCH <<< "$VERSION"

mkdir -p build dist

echo "== core tests"
g++ -std=c++17 -Wall -Wextra -Werror -O1 -Isrc tests/core_test.cpp -o build/core_test
./build/core_test

echo "== windows exes"
cat > build/version.h <<EOF
#define EVEWT_VERSION_COMMA $MAJOR,$MINOR,$PATCH,0
#define EVEWT_VERSION_STRING "$VERSION"
EOF
CXX=x86_64-w64-mingw32-g++
WINDRES=x86_64-w64-mingw32-windres
CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror -municode -DUNICODE -D_UNICODE -Isrc -static -s)

"$WINDRES" -I. -Isrc -Ibuild src/evewt.rc -O coff -o build/evewt.res.o
"$WINDRES" -I. -Isrc -Ibuild -DEVEWT_CONSOLE src/evewt.rc -O coff -o build/evewt-console.res.o
"$CXX" "${CXXFLAGS[@]}" -mwindows src/evewt.cpp build/evewt.res.o -o build/evewt.exe -lshell32 -lole32 -ladvapi32 -lws2_32
"$CXX" "${CXXFLAGS[@]}" -mconsole src/evewt-console.cpp build/evewt-console.res.o -o build/evewt-console.exe -lws2_32

echo "== msi"
msi="dist/EVE-Windows-Terminal-$VERSION.msi"
wixl --arch x64 --define "Version=$VERSION" --output "$msi" EVE-WT.wxs
ls -l build/evewt.exe build/evewt-console.exe "$msi"
