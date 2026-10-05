if(NOT ((WIN32 AND MSVC) OR APPLE))
    return()
endif()

add_library(catro_rtc STATIC
    src/room_mesh_transport.cpp
)
if(APPLE)
    target_sources(catro_rtc PRIVATE src/macos_trust.mm)
    target_link_libraries(catro_rtc PRIVATE "-framework Foundation" "-framework Security")
    target_compile_options(catro_rtc PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
endif()
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

if(WIN32 AND MSVC)
    target_compile_definitions(catro_rtc PRIVATE
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
endif()

catro_enable_warnings(catro_rtc)
