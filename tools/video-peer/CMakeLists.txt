if(NOT (WIN32 AND MSVC))
    return()
endif()

add_library(catro_video_peer_cli STATIC video_peer.cpp)
target_include_directories(catro_video_peer_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_video_peer_cli
    PUBLIC Catro::Video Catro::Windows catro_voice_peer_transport
)
target_compile_definitions(catro_video_peer_cli PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
catro_enable_warnings(catro_video_peer_cli)

add_executable(catro_video_peer main.cpp)
set_target_properties(catro_video_peer PROPERTIES OUTPUT_NAME "catro-video-peer")
target_link_libraries(catro_video_peer PRIVATE catro_video_peer_cli)
target_compile_definitions(catro_video_peer PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
catro_enable_warnings(catro_video_peer)
