catro_enable_voice_dependency()

add_library(catro_voice STATIC
    src/codec.cpp
    src/jitter.cpp
    src/packet.cpp
)
add_library(Catro::Voice ALIAS catro_voice)

target_include_directories(catro_voice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(catro_voice PRIVATE Opus::opus)
catro_enable_warnings(catro_voice)
