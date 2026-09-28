if(NOT (WIN32 AND MSVC))
    return()
endif()

add_library(catro_screen_runtime STATIC
    screen_runtime.cpp
)
add_library(Catro::ScreenRuntime ALIAS catro_screen_runtime)

target_include_directories(catro_screen_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(catro_screen_runtime
    PUBLIC
        Catro::Windows
        Catro::Audio
        Catro::Voice
        Catro::Video
        Catro::Transport
        catro_room_runtime
)

target_compile_definitions(catro_screen_runtime PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)

catro_enable_warnings(catro_screen_runtime)
