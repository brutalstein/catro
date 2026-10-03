catro_enable_voice_dependency()
catro_enable_voice_processing_dependency()

add_library(catro_voice STATIC
    src/audio_bridge.cpp
    src/codec.cpp
    src/jitter.cpp
    src/packet.cpp
    src/pipeline.cpp
    src/stream_controls.cpp
    src/voice_processor.cpp
)
add_library(Catro::Voice ALIAS catro_voice)

target_include_directories(catro_voice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(catro_voice
    PUBLIC Catro::Audio
    PRIVATE Opus::opus catro_webrtc_apm
)
catro_enable_warnings(catro_voice)
