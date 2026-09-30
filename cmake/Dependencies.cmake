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
    if(MSVC)
        # Opus defaults to the DLL CRT even when the parent project uses /MT. Keep every native
        # dependency on the same self-contained runtime to avoid Debug unresolved __imp_* symbols.
        set(OPUS_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    endif()
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_CUSTOM_MODES OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(opus)
endfunction()


# Production RTC transport.
#
# libdatachannel provides ICE/STUN/TURN, DTLS-SRTP media transport, and WebSocket signaling.
# Pin every third-party revision; do not follow a moving branch in reproducible Release builds.
if((WIN32 AND MSVC) OR APPLE)
    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG v3.6.5
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )

    FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG 443f6934d9007eb7076ab7825ba330f355fcbead
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )

    function(catro_enable_rtc_dependency)
        # Mbed TLS is built into Catro so target machines do not need a separately installed TLS stack.
        set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
        set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(mbedtls)

        # libdatachannel's DTLS transport always references the RFC 5764 SRTP profile helpers,
        # including for data-channel-only builds. Mbed TLS 3 keeps that API behind this config
        # macro, so compile and export the feature consistently through its public TLS target.
        if(TARGET mbedtls)
            target_compile_definitions(mbedtls PUBLIC MBEDTLS_SSL_DTLS_SRTP)
        endif()

        # libdatachannel's Mbed TLS lookup accepts this compatibility target.
        if(TARGET mbedtls AND NOT TARGET MbedTLS::MbedTLS)
            add_library(MbedTLS::MbedTLS ALIAS mbedtls)
        endif()

        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_DEPS_LIBS OFF CACHE BOOL "" FORCE)
        set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
        set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
        set(USE_NICE OFF CACHE BOOL "" FORCE)
        set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
        set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)
        # Catro transports its own bounded Opus/H.264 datagrams over WebRTC DataChannels. It does
        # not use libdatachannel media Tracks/SRTP, so compiling that layer only adds libsrtp and a
        # second crypto-discovery path without providing any runtime capability.
        set(NO_MEDIA ON CACHE BOOL "" FORCE)
        set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
        set(NO_TESTS ON CACHE BOOL "" FORCE)
        set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(libdatachannel)
    endfunction()
endif()
