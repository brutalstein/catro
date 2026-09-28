if(NOT (WIN32 AND MSVC))
    return()
endif()

add_library(catro_room_runtime SHARED
    src/room_runtime.cpp
)

target_include_directories(catro_room_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(catro_room_runtime
    PRIVATE
        Catro::Rtc
)

target_compile_definitions(catro_room_runtime PRIVATE
    CATRO_ROOM_RUNTIME_EXPORTS
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

set_target_properties(catro_room_runtime PROPERTIES
    OUTPUT_NAME "catro-room-runtime"
)

catro_enable_warnings(catro_room_runtime)
