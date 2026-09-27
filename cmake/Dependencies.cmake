include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b
    GIT_PROGRESS TRUE
    # Third-party headers are not held to Catro's warnings-as-errors policy.
    SYSTEM
)

function(catro_enable_reporting_dependency)
    FetchContent_MakeAvailable(nlohmann_json)
endfunction()

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 95d8a61b089317bec800c7cc4c64064cbcb3802d
    GIT_PROGRESS TRUE
)

function(catro_enable_test_dependencies)
    FetchContent_MakeAvailable(Catch2)
endfunction()


FetchContent_Declare(
    opus
    URL https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
    URL_HASH SHA256=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

function(catro_enable_voice_dependency)
    # Keep the embedded reference codec minimal; Catro owns its own tests and tools.
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_CUSTOM_MODES OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(opus)
endfunction()
