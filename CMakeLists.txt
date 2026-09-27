cmake_minimum_required(VERSION 3.28)

project(Catro VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Catro uses headers, not C++20 modules; skip per-source module dependency scanning.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Tools run side by side: the report tool starts the probe helper from its own directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

option(CATRO_BUILD_TESTS "Build Catro tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

catro_enable_reporting_dependency()

add_subdirectory(core/capabilities)
add_subdirectory(core/reporting)

# Native Windows layers target the MSVC toolchain; other Windows compilers build the portable core only.
if(WIN32 AND MSVC)
    add_subdirectory(platform/windows)
    add_subdirectory(tools/capability-probe)
    add_subdirectory(apps/windows)
endif()
add_subdirectory(tools/capability-report)

if(CATRO_BUILD_TESTS)
    enable_testing()
    catro_enable_test_dependencies()
    add_subdirectory(tests)
endif()
