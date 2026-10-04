cmake_minimum_required(VERSION 3.28)

project(Catro VERSION 0.3.1 LANGUAGES C CXX)

# Catro ships through scripts/package-windows.ps1 and the native app bundles; it does not use
# CMake's install tree. Disable install-rule generation globally so embedded third-party projects
# cannot make configure/generate fail on irrelevant install(EXPORT) dependency bookkeeping.
set(CMAKE_SKIP_INSTALL_RULES ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    # Production Windows artifacts are self-contained: avoid a target-machine dependency on
    # vc_redist by linking the MSVC runtime statically in every CMake-built native binary.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
# Catro uses headers, not C++20 modules; skip per-source module dependency scanning.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# The macOS platform layer is Objective-C++ at its native boundary.
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    set(CMAKE_OBJCXX_SCAN_FOR_MODULES OFF)
endif()

# Tools run side by side: the report tool starts the probe helper from its own directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

option(CATRO_BUILD_TESTS "Build Catro tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

catro_enable_reporting_dependency()

add_subdirectory(core/capabilities)
add_subdirectory(core/reporting)
add_subdirectory(core/audio)
add_subdirectory(core/voice)
add_subdirectory(core/video)
add_subdirectory(core/transport)
add_subdirectory(core/community)
if((WIN32 AND MSVC) OR APPLE)
    catro_enable_rtc_dependency()
    add_subdirectory(core/rtc)
endif()
add_subdirectory(apps/diagnostics)
add_subdirectory(apps/shell)
# Portable translations build everywhere; native layers only with their platform toolchain.
add_subdirectory(platform/macos)
add_subdirectory(platform/windows)

# Native Windows layers target the MSVC toolchain; other Windows compilers build the portable core only.
if(WIN32 AND MSVC)
    add_subdirectory(tools/capability-probe)
elseif(APPLE)
    add_subdirectory(tools/capability-probe)
    # Swift needs the Xcode or Ninja generator; other generators build everything but the shell.
    if(CMAKE_GENERATOR MATCHES "Xcode|Ninja")
        add_subdirectory(apps/macos)
    endif()
endif()
add_subdirectory(tools/capability-report)
add_subdirectory(tools/audio-check)
add_subdirectory(tools/capture-check)
add_subdirectory(tools/encode-check)
add_subdirectory(tools/voice-peer)
add_subdirectory(tools/video-peer)
add_subdirectory(apps/room-runtime/windows)
add_subdirectory(apps/voice-runtime/common)
add_subdirectory(apps/voice-runtime/windows)
add_subdirectory(apps/voice-runtime/macos)
add_subdirectory(apps/screen-runtime/common)
add_subdirectory(apps/screen-runtime/windows)
add_subdirectory(apps/screen-runtime/macos)
add_subdirectory(apps/product-session/macos)

if(CATRO_BUILD_TESTS)
    enable_testing()
    catro_enable_test_dependencies()
    add_subdirectory(tests)
endif()
