# Plain C++ translation from native records to domain facts. Built on every platform so its rules
# are tested with any toolchain.
add_library(catro_windows_translation STATIC src/windows_translation.cpp)
target_include_directories(catro_windows_translation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(catro_windows_translation PUBLIC Catro::Capabilities)
catro_enable_warnings(catro_windows_translation)

# Native Windows layers target the MSVC toolchain.
if(NOT (WIN32 AND MSVC))
    return()
endif()

add_library(catro_platform_windows STATIC
    src/audio_platform.cpp
    src/screen_capture.cpp
    src/video_encoder.cpp
    src/local_state.cpp
    src/capability_service.cpp
    src/process_probe_executor.cpp
    src/probe_dispatch.cpp
    src/probes/system_probe.cpp
    src/probes/runtime_probe.cpp
    src/probes/gpu_display_probe.cpp
    src/probes/encoder_probe.cpp
    src/probes/audio_probe.cpp
)
add_library(Catro::Windows ALIAS catro_platform_windows)

target_include_directories(catro_platform_windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(catro_platform_windows
    PUBLIC Catro::Capabilities Catro::Audio Catro::Community
    PRIVATE catro_windows_translation Catro::Reporting powrprof user32 bcrypt shell32 wtsapi32 dxgi d3d11 d3d12 dxcore shcore windowsapp
            mfplat mfuuid ole32 avrt
)
target_compile_definitions(catro_platform_windows PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
catro_enable_warnings(catro_platform_windows)
