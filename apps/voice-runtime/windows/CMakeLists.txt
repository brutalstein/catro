if(WIN32 AND MSVC)
    add_library(catro_voice_runtime SHARED src/voice_runtime.cpp)
    target_include_directories(catro_voice_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(catro_voice_runtime PRIVATE catro_voice_peer_cli Catro::Windows)
    target_compile_definitions(catro_voice_runtime PRIVATE
        CATRO_VOICE_RUNTIME_EXPORTS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
    set_target_properties(catro_voice_runtime PROPERTIES OUTPUT_NAME "catro-voice-runtime")
    catro_enable_warnings(catro_voice_runtime)
endif()
