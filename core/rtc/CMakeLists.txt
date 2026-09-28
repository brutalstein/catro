if(NOT (WIN32 AND MSVC))
    return()
endif()

add_library(catro_rtc STATIC
    src/room_mesh_transport.cpp
)
add_library(Catro::Rtc ALIAS catro_rtc)

target_include_directories(catro_rtc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(catro_rtc
    PUBLIC
        LibDataChannel::LibDataChannelStatic
    PRIVATE
        nlohmann_json::nlohmann_json
)

target_compile_definitions(catro_rtc PRIVATE
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

catro_enable_warnings(catro_rtc)
