add_library(catro_voice_peer_transport INTERFACE)
target_include_directories(catro_voice_peer_transport INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_voice_peer_transport INTERFACE Catro::Transport)

add_library(catro_voice_peer_cli STATIC
    voice_peer.cpp
    worker_priority.cpp
)
target_include_directories(catro_voice_peer_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_voice_peer_cli
    PUBLIC Catro::Voice Catro::Audio catro_voice_peer_transport
)
if(WIN32)
    target_link_libraries(catro_voice_peer_cli PUBLIC avrt)
    target_compile_definitions(catro_voice_peer_cli PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endif()
catro_enable_warnings(catro_voice_peer_cli)

if(WIN32 AND MSVC)
    add_executable(catro_voice_peer main.cpp)
    set_target_properties(catro_voice_peer PROPERTIES OUTPUT_NAME "catro-voice-peer")
    target_link_libraries(catro_voice_peer PRIVATE catro_voice_peer_cli Catro::Windows)
    target_compile_definitions(catro_voice_peer PRIVATE NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    catro_enable_warnings(catro_voice_peer)
elseif(APPLE)
    add_executable(catro_voice_peer main.cpp)
    set_target_properties(catro_voice_peer PROPERTIES OUTPUT_NAME "catro-voice-peer")
    target_link_libraries(catro_voice_peer PRIVATE catro_voice_peer_cli Catro::Macos)
    catro_enable_warnings(catro_voice_peer)
endif()
