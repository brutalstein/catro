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


# Voice processing: WebRTC's AudioProcessing module (AEC3 echo cancellation, noise suppression,
# AGC2), the processing Chrome and most voice apps ship. It is built from source with Catro's own
# CMake source list (cmake/WebRtcApmSources.cmake), so no meson toolchain is needed.
FetchContent_Declare(
    abseil
    URL https://github.com/abseil/abseil-cpp/releases/download/20240722.0/abseil-cpp-20240722.0.tar.gz
    URL_HASH SHA256=f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)

FetchContent_Declare(
    webrtc_apm
    GIT_REPOSITORY https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing.git
    GIT_TAG 846fe90a289f58b7c9303a635142aa2c7caa93e5 # v2.1
    GIT_PROGRESS TRUE
)

function(catro_enable_voice_processing_dependency)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    # Catro links the static MSVC runtime (/MT); Abseil must match.
    set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(abseil)
    FetchContent_MakeAvailable(webrtc_apm)

    include(${PROJECT_SOURCE_DIR}/cmake/WebRtcApmSources.cmake)
    set(root ${webrtc_apm_SOURCE_DIR})
    foreach(group COMMON SSE2 AVX2 NEON)
        list(TRANSFORM CATRO_WEBRTC_APM_${group}_SOURCES PREPEND ${root}/)
    endforeach()

    add_library(catro_webrtc_apm STATIC ${CATRO_WEBRTC_APM_COMMON_SOURCES})
    # Upstream builds and tests this code as C++17.
    set_target_properties(catro_webrtc_apm PROPERTIES CXX_STANDARD 17 C_STANDARD 11)
    target_include_directories(catro_webrtc_apm SYSTEM PUBLIC ${root}/webrtc)
    target_compile_definitions(catro_webrtc_apm
        PUBLIC WEBRTC_LIBRARY_IMPL WEBRTC_APM_DEBUG_DUMP=0
        PRIVATE NDEBUG _WINSOCKAPI_ _GNU_SOURCE)
    target_link_libraries(catro_webrtc_apm PUBLIC
        absl::base absl::core_headers absl::strings absl::any_invocable absl::algorithm_container)

    if(WIN32)
        target_compile_definitions(catro_webrtc_apm
            PUBLIC WEBRTC_WIN NOMINMAX _USE_MATH_DEFINES
            PRIVATE __STDC_FORMAT_MACROS=1)
        target_link_libraries(catro_webrtc_apm PRIVATE winmm)
    else()
        target_compile_definitions(catro_webrtc_apm PUBLIC WEBRTC_POSIX)
        if(APPLE)
            target_compile_definitions(catro_webrtc_apm PUBLIC WEBRTC_MAC)
            target_link_libraries(catro_webrtc_apm PRIVATE "-framework Foundation")
        endif()
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
        # AVX2 kernels are compiled separately and chosen at run time, so older CPUs still work.
        target_sources(catro_webrtc_apm PRIVATE
            ${CATRO_WEBRTC_APM_SSE2_SOURCES} ${CATRO_WEBRTC_APM_AVX2_SOURCES})
        if(MSVC)
            set(avx2_flags /arch:AVX2)
        else()
            set(avx2_flags -mavx2 -mfma)
        endif()
        set_source_files_properties(${CATRO_WEBRTC_APM_AVX2_SOURCES}
            PROPERTIES COMPILE_OPTIONS "${avx2_flags}")
        target_compile_definitions(catro_webrtc_apm PUBLIC WEBRTC_ENABLE_AVX2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
        target_sources(catro_webrtc_apm PRIVATE ${CATRO_WEBRTC_APM_NEON_SOURCES})
        target_compile_definitions(catro_webrtc_apm PUBLIC WEBRTC_ARCH_ARM64 WEBRTC_HAS_NEON)
    endif()
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
