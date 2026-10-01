if(NOT APPLE)
    return()
endif()

# Full-duplex room screen sharing over ScreenCaptureKit, VideoToolbox and Core Animation.
add_library(catro_screen_runtime_macos STATIC src/macos_screen_runtime.mm)
target_include_directories(catro_screen_runtime_macos PUBLIC include)
target_link_libraries(catro_screen_runtime_macos
    PUBLIC catro_screen_transport Catro::Macos
    PRIVATE catro_room_runtime "-framework AudioToolbox"
)
catro_enable_warnings(catro_screen_runtime_macos)
