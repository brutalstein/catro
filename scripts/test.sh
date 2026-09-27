#!/bin/sh
# Runs the CTest suite of a build made by build.sh. Usage: scripts/test.sh [Debug|Release] [regex]
set -eu
configuration=${1:-Debug}
preset=${CATRO_PRESET:-macos-clang}
root=$(cd "$(dirname "$0")/.." && pwd)

if [ $# -ge 2 ]; then
    exec ctest --test-dir "$root/out/build/$preset" -C "$configuration" --output-on-failure -R "$2"
fi
exec ctest --test-dir "$root/out/build/$preset" -C "$configuration" --output-on-failure
