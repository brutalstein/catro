#!/bin/sh
# Configures and builds the core, tools, tests, and the SwiftUI shell. Writes only under out/.
# Usage: scripts/build.sh [Debug|Release]   CATRO_PRESET=macos-universal for an arm64 + x86_64 build.
set -eu
configuration=${1:-Debug}
preset=${CATRO_PRESET:-macos-clang}
root=$(cd "$(dirname "$0")/.." && pwd)

cd "$root"
cmake --preset "$preset"
cmake --build "out/build/$preset" --config "$configuration"
echo "Shell: out/build/$preset/$configuration/Catro.app"
