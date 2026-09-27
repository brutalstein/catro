# Portable arguments, session timing, and summary; tests link it with a fake platform.
add_library(catro_audio_check_cli STATIC audio_check.cpp)
target_include_directories(catro_audio_check_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_audio_check_cli PUBLIC Catro::Audio)
catro_enable_warnings(catro_audio_check_cli)

if(WIN32 AND MSVC)
    add_executable(catro_audio_check main.cpp)
    set_target_properties(catro_audio_check PROPERTIES OUTPUT_NAME "catro-audio-check")
    target_link_libraries(catro_audio_check PRIVATE catro_audio_check_cli Catro::Windows)
    target_compile_definitions(catro_audio_check PRIVATE NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    catro_enable_warnings(catro_audio_check)
elseif(APPLE)
    # Unverified until a Mac build runs. macOS attributes microphone access to the terminal app.
    add_executable(catro_audio_check main.cpp)
    set_target_properties(catro_audio_check PROPERTIES OUTPUT_NAME "catro-audio-check")
    target_link_libraries(catro_audio_check PRIVATE catro_audio_check_cli Catro::Macos)
    catro_enable_warnings(catro_audio_check)
endif()
