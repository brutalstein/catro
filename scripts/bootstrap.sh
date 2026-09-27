#!/bin/sh
# Checks macOS build prerequisites. Reports what is missing; never installs or changes anything.
set -u
missing=""

if command -v cmake >/dev/null 2>&1; then
    version=$(cmake --version | head -n 1 | sed 's/[^0-9.]//g')
    major=${version%%.*}
    rest=${version#*.}
    minor=${rest%%.*}
    if [ "$major" -lt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -lt 28 ]; }; then
        missing="$missing\n  - CMake 3.28 or newer (found $version)"
    fi
else
    missing="$missing\n  - CMake 3.28 or newer on PATH (https://cmake.org/download/)"
fi

if ! xcodebuild -version >/dev/null 2>&1; then
    missing="$missing\n  - Xcode 16 or newer, selected with xcode-select (Command Line Tools alone cannot build the shell)"
fi
if ! xcrun --show-sdk-path >/dev/null 2>&1; then
    missing="$missing\n  - A macOS SDK reachable through xcrun"
fi
if ! xcrun --find swiftc >/dev/null 2>&1; then
    missing="$missing\n  - The Swift toolchain bundled with Xcode"
fi

if [ -n "$missing" ]; then
    printf 'Missing prerequisites:%b\n' "$missing"
    echo 'Install them yourself; these scripts never install tools or change system settings.'
    exit 1
fi
echo "CMake $version, $(xcodebuild -version | head -n 1), SDK $(xcrun --show-sdk-version)"
echo 'Prerequisites present.'
