if(NOT APPLE)
    return()
endif()

# Directory, voice and screen session state machine behind the macOS SwiftUI product shell.
add_library(catro_product_session_macos STATIC src/macos_product_session.cpp)
target_include_directories(catro_product_session_macos PUBLIC include)
target_link_libraries(catro_product_session_macos
    PUBLIC Catro::Shell Catro::Community Catro::Macos catro_screen_runtime_macos catro_voice_runtime
    PRIVATE catro_room_runtime
)
catro_enable_warnings(catro_product_session_macos)
