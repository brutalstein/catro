if(WIN32 AND MSVC)
    add_library(catro_voice_runtime SHARED src/voice_runtime.cpp)
    target_link_libraries(catro_voice_runtime
        PUBLIC catro_room_voice
        PRIVATE catro_room_runtime Catro::Windows
    )
    target_compile_definitions(catro_voice_runtime PRIVATE
        CATRO_VOICE_RUNTIME_EXPORTS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
    set_target_properties(catro_voice_runtime PROPERTIES OUTPUT_NAME "catro-voice-runtime")
    catro_enable_warnings(catro_voice_runtime)
endif()
