if(NOT APPLE)
    return()
endif()

# Directory, voice and screen session state machine behind the macOS SwiftUI product shell.
add_library(catro_product_session_macos STATIC
    src/macos_product_session.cpp
    src/macos_adaptive_media.cpp
)
target_include_directories(catro_product_session_macos PUBLIC include)
target_link_libraries(catro_product_session_macos
    PUBLIC Catro::Shell Catro::Community Catro::Macos catro_screen_runtime_macos catro_voice_runtime
    PRIVATE catro_room_runtime
)
catro_enable_warnings(catro_product_session_macos)

# Objective-C++ bridge Swift imports; kept beside the session so the Makefile sanitizer build (which
# skips the Swift app) still compiles and tests it.
add_library(catro_product_bridge_macos STATIC bridge/CatroProductBridge.mm)
target_include_directories(catro_product_bridge_macos PUBLIC bridge)
target_link_libraries(catro_product_bridge_macos
    PUBLIC catro_product_session_macos "-framework Foundation" "-framework QuartzCore")
target_compile_options(catro_product_bridge_macos PRIVATE -fobjc-arc)
catro_enable_warnings(catro_product_bridge_macos)
