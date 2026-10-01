# Portable screen-share transport shared by the Windows and macOS runtimes. It owns no OS media type:
# room media arrives through RoomScreenApi and native codecs/presenters through small interfaces.
add_library(catro_screen_transport STATIC src/screen_transport_runtime.cpp)
target_include_directories(catro_screen_transport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # Header only: the transport calls the room C ABI through RoomScreenApi, never by linking it.
    ${PROJECT_SOURCE_DIR}/apps/room-runtime/windows/include
)
target_link_libraries(catro_screen_transport PUBLIC
    Catro::Audio
    Catro::Transport
    Catro::Video
    Catro::Voice
)
catro_enable_warnings(catro_screen_transport)
