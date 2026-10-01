if(NOT APPLE)
    return()
endif()

# Directory session state machine behind the macOS SwiftUI product shell.
add_library(catro_product_session_macos STATIC src/macos_product_session.cpp)
target_include_directories(catro_product_session_macos PUBLIC include)
target_link_libraries(catro_product_session_macos PUBLIC Catro::Shell Catro::Community Catro::Macos)
catro_enable_warnings(catro_product_session_macos)
