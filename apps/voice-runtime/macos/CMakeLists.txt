if(NOT APPLE)
    return()
endif()

# The CatroVoiceRuntime* C ABI over CoreAudio. Static like the Apple room runtime; the Swift shell
# links it directly.
add_library(catro_voice_runtime STATIC src/voice_runtime.mm)
target_link_libraries(catro_voice_runtime
    PUBLIC catro_room_voice
    PRIVATE catro_room_runtime Catro::Macos
)
set_target_properties(catro_voice_runtime PROPERTIES OUTPUT_NAME "catro-voice-runtime")
catro_enable_warnings(catro_voice_runtime)
