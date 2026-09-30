# Portable production voice loop shared by the Windows and macOS C ABI adapters. It owns no OS type:
# audio arrives through audio::AudioPlatform and room media through RoomVoiceApi.
add_library(catro_room_voice STATIC src/room_voice_runtime.cpp)
target_include_directories(catro_room_voice PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # Header only: the loop calls the room C ABI through RoomVoiceApi, never by linking it.
    ${PROJECT_SOURCE_DIR}/apps/room-runtime/windows/include
)
target_link_libraries(catro_room_voice PUBLIC catro_voice_peer_cli)
catro_enable_warnings(catro_room_voice)
