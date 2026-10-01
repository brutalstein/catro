# Plain C++ translation from native records to domain facts. Built on every platform so it
# compiles and is tested without an Apple SDK.
add_library(catro_macos_translation STATIC src/macos_translation.cpp)
target_include_directories(catro_macos_translation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(catro_macos_translation PUBLIC Catro::Capabilities)
catro_enable_warnings(catro_macos_translation)

if(NOT APPLE)
    return()
endif()

add_library(catro_platform_macos STATIC
    src/audio_platform.mm
    src/directory_client.mm
    src/local_state.mm
    src/capability_service.mm
    src/process_probe_executor.mm
    src/probe_dispatch.mm
    src/probe_support.mm
    src/probes/system_probe.mm
    src/probes/runtime_probe.mm
    src/probes/gpu_display_probe.mm
    src/probes/encoder_probe.mm
    src/probes/audio_probe.mm
    src/screen_capture.mm
    src/video_encoder.mm
    src/video_decoder.mm
    src/video_presenter.mm
)
add_library(Catro::Macos ALIAS catro_platform_macos)

target_include_directories(catro_platform_macos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(catro_platform_macos
    PUBLIC Catro::Capabilities Catro::Audio Catro::Community Catro::Video "-framework CoreVideo"
    PRIVATE catro_macos_translation Catro::Reporting
            nlohmann_json::nlohmann_json
            "-framework Foundation" "-framework AppKit" "-framework CoreGraphics" "-framework CoreAudio"
            "-framework IOKit" "-framework Metal" "-framework VideoToolbox" "-framework CoreMedia"
            "-framework AudioToolbox" "-framework AVFoundation" "-framework Security"
            "-framework ScreenCaptureKit" "-framework QuartzCore" "-framework IOSurface"
)
target_compile_options(catro_platform_macos PRIVATE -fobjc-arc)
catro_enable_warnings(catro_platform_macos)
